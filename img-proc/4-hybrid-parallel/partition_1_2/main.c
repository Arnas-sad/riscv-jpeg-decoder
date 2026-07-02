/*
 * JPEG HYBRID-PARALLEL decoder (2d) - IQZZ+IDCT WORKER (tile 1).
 *
 * TWO copies run on tile 1 (partition 1 = worker A, partition 2 = worker B,
 * selected from PARTITION_ID). The source deals coefficient tokens round-robin
 * (even -> A, odd -> B); each worker fuses IQZZ+IDCT locally on its share (no
 * inter-stage ring hop) and forwards pixel tokens to the CC+raster sink.
 * Stateless; forwards `last`. The two workers satisfy the multiple-partitions-
 * per-tile requirement. S-02: only nr_data_units blocks copied.
 */
#include <stdint.h>
#include <string.h>
#include <timers.h>
#include <xil_printf.h>
#include <platform.h>
#include <fifo.h>
#include <vep_shared_memory_regions.h>
#include "structures.h"
#include "actors.h"
#include "pipeline.h"

#define MEM     vep_memshared0_shared_region
#define KCYC(x) ((uint32_t)((x) >> 10))

/* Branch selection: partition 1 serves branch A, partition 2 branch B. */
#if PARTITION_ID == 1
#define F_IN       (&MEM->f_freq_a)
#define F_OUT      (&MEM->f_pix_a)
#define B_OUT      (MEM->b_pix_a)
#define STAGE_NAME "idct_a"
#else
#define F_IN       (&MEM->f_freq_b)
#define F_OUT      (&MEM->f_pix_b)
#define B_OUT      (MEM->b_pix_b)
#define STAGE_NAME "idct_b"
#endif

int main(void) {
    xil_printf("%d %d: 2d IQZZ+IDCT worker %s starting\n",
               TILE_ID, PARTITION_ID, STAGE_NAME);
    _Static_assert(sizeof(tok_pix_t) <= TOK_PIX_SIZE,
                   "tok_pix_t larger than TOK_PIX_SIZE");

    fifo_init(F_OUT, B_OUT, PIPE_DEPTH, sizeof(tok_pix_t));
    while (!fifo_initialized(F_IN)) asm("wfi");

    static FValue fvloc[10];
    static FBlock fbtmp;
    static PBlock pbloc[10];
    SubHeader1 sh1 = {0};
    SubHeader2 sh2 = {0};
    int nr_du = 0;                 /* blocks/MCU for this token (S-01) */

    uint64_t work = 0, wait_in = 0, wait_out = 0;
    uint32_t wcet_work = 0;
    uint32_t bcet_work = 0xFFFFFFFFu, peak_occ = 0;
    uint64_t t0, t1, t2, t3, t4;
    uint32_t firing;
    uint32_t n = 0;

    uint64_t g_start  = read_global_timer();
    uint64_t pt_start = read_partition_timer();

    for (;;) {
        t0 = read_partition_timer();
        tok_coef_t volatile *in = (tok_coef_t volatile *)fifo_claim_token(F_IN);
        t1 = read_partition_timer();
        wait_in += t1 - t0;

        int last = in->last;
        if (!last) {
            sh1 = in->sh1; sh2 = in->sh2;
            nr_du = in->nr_data_units;
            /* S-02: only the valid blocks - shared-memory copies dominate. */
            memcpy(fvloc, (const void *)in->fv, (size_t)nr_du * sizeof(FValue));
        }
        fifo_release_space(F_IN);
        t2 = read_partition_timer();
        work += t2 - t1;

        tok_pix_t volatile *out = (tok_pix_t volatile *)fifo_claim_space(F_OUT);
        t3 = read_partition_timer();
        wait_out += t3 - t2;

        if (last) { out->last = 1; fifo_release_token(F_OUT); break; }

        out->last = 0; out->sh1 = sh1; out->sh2 = sh2;
        /* fused iqzz+idct - all local, no ring hop between them */
        for (int i = 0; i < nr_du; i++) {
            iqzz(&fvloc[i], &fbtmp);
            idct(&fbtmp, &pbloc[i]);
        }
        memcpy((void *)out->pb, pbloc, (size_t)nr_du * sizeof(PBlock));  /* S-02 */
        fifo_release_token(F_OUT);
        t4 = read_partition_timer();
        work += t4 - t3;

        firing = (uint32_t)((t2 - t1) + (t4 - t3));
        if (firing > wcet_work) wcet_work = firing;
        if (firing < bcet_work) bcet_work = firing;
        { uint32_t o = fifo_tokens(F_OUT); if (o > peak_occ) peak_occ = o; }
        n++;
    }

    uint64_t pt_end = read_partition_timer();
    uint64_t g_end  = read_global_timer();
    uint64_t rt = g_end - g_start, et = pt_end - pt_start;

    xil_printf("%d %d: worker %s done - %u MCUs\n",
               TILE_ID, PARTITION_ID, STAGE_NAME, n);

    xil_printf("BENCH impl=hybrid-parallel stage=%s tile=%d partition=%d n_mcus=%u jpeg_size=0\n",
               STAGE_NAME, TILE_ID, PARTITION_ID, n);
    xil_printf("BENCH xsize=%d ysize=%d mcu_sx=%d mcu_sy=%d n_comp=%d\n",
               sh2.x_size, sh2.y_size, sh2.MCU_sx, sh2.MCU_sy, sh1.n_comp);
    xil_printf("BENCH rt_kcyc=%u rt_ms=%u et_kcyc=%u et_ms=%u\n",
               KCYC(rt), (uint32_t)(rt / (FREQUENCY / 1000)),
               KCYC(et), (uint32_t)(et / (FREQUENCY / 1000)));
    xil_printf("BENCH gstart_hi=%u gstart_lo=%u gend_hi=%u gend_lo=%u\n",
               (uint32_t)(g_start >> 32), (uint32_t)g_start,
               (uint32_t)(g_end   >> 32), (uint32_t)g_end);
    xil_printf("BENCH wait_in_kcyc=%u wait_out_kcyc=%u work_kcyc=%u wcet_work=%u\n",
               KCYC(wait_in), KCYC(wait_out), KCYC(work), wcet_work);
    xil_printf("BENCH bcet_work=%u peak_occ=%u depth=%u\n", bcet_work, peak_occ, PIPE_DEPTH);
    xil_printf("BENCH_DONE\n");

    while (1) asm("wfi");
}
