/*
 * JPEG FUNCTION-PARALLEL decoder (2c) - middle_b: CC (tile 1, partition 2).
 *
 * f_pix in -> colour-convert the MCU into a BGR(A) ColorBuffer -> f_col out.
 * Stateless; forwards the `last` sentinel. Second partition on tile 1 (the
 * multiple-partitions-per-tile requirement).
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

int main(void) {
    xil_printf("%d %d: 2c middle_b (cc) starting\n", TILE_ID, PARTITION_ID);
    _Static_assert(sizeof(tok_col_t) <= TOK_COL_SIZE,
                   "tok_col_t larger than TOK_COL_SIZE");

    fifo_init(&MEM->f_col, MEM->b_col, PIPE_DEPTH, sizeof(tok_col_t));
    while (!fifo_initialized(&MEM->f_pix)) asm("wfi");

    static PBlock      pbloc[10];
    static ColorBuffer cbloc;
    SubHeader1 sh1 = {0};
    SubHeader2 sh2 = {0};
    int nr_du = 0;

    uint64_t work = 0, wait_in = 0, wait_out = 0;
    uint32_t wcet_work = 0, bcet_work = 0xFFFFFFFFu, peak_occ = 0;
    uint64_t t0, t1, t2, t3, t4;
    uint32_t firing, n = 0;

    uint64_t g_start  = read_global_timer();
    uint64_t pt_start = read_partition_timer();

    for (;;) {
        t0 = read_partition_timer();
        tok_pix_t volatile *in = (tok_pix_t volatile *)fifo_claim_token(&MEM->f_pix);
        t1 = read_partition_timer();
        wait_in += t1 - t0;

        int last = in->last;
        if (!last) {
            sh1 = in->sh1; sh2 = in->sh2;
            nr_du = in->nr_data_units;
            memcpy(pbloc, (const void *)in->pb, (size_t)nr_du * sizeof(PBlock));
        }
        fifo_release_space(&MEM->f_pix);
        t2 = read_partition_timer();
        work += t2 - t1;

        tok_col_t volatile *out = (tok_col_t volatile *)fifo_claim_space(&MEM->f_col);
        t3 = read_partition_timer();
        wait_out += t3 - t2;

        if (last) { out->last = 1; fifo_release_token(&MEM->f_col); break; }

        out->last = 0; out->sh2 = sh2;
        cc(&sh1, pbloc, &cbloc);
        {
            size_t cb_bytes = (sh1.n_comp == 1)
                            ? 64u
                            : (size_t)(3 * sh1.MCU_sx * sh1.MCU_sy);
            memcpy((void *)out->cb.data, cbloc.data, cb_bytes);
        }
        fifo_release_token(&MEM->f_col);
        t4 = read_partition_timer();
        work += t4 - t3;

        firing = (uint32_t)((t2 - t1) + (t4 - t3));
        if (firing > wcet_work) wcet_work = firing;
        if (firing < bcet_work) bcet_work = firing;
        { uint32_t o = fifo_tokens(&MEM->f_col); if (o > peak_occ) peak_occ = o; }
        n++;
    }

    uint64_t pt_end = read_partition_timer();
    uint64_t g_end  = read_global_timer();
    uint64_t rt = g_end - g_start, et = pt_end - pt_start;

    xil_printf("%d %d: middle_b done - %u MCUs\n", TILE_ID, PARTITION_ID, n);
    xil_printf("BENCH impl=function-parallel stage=cc tile=%d partition=%d n_mcus=%u jpeg_size=0\n",
               TILE_ID, PARTITION_ID, n);
    xil_printf("BENCH xsize=%d ysize=%d mcu_sx=%d mcu_sy=%d n_comp=%d\n",
               sh2.x_size, sh2.y_size, sh2.MCU_sx, sh2.MCU_sy, sh1.n_comp);
    xil_printf("BENCH rt_kcyc=%u rt_ms=%u et_kcyc=%u et_ms=%u\n",
               KCYC(rt), (uint32_t)(rt / (FREQUENCY / 1000)),
               KCYC(et), (uint32_t)(et / (FREQUENCY / 1000)));
    xil_printf("BENCH wait_in_kcyc=%u wait_out_kcyc=%u work_kcyc=%u wcet_work=%u\n",
               KCYC(wait_in), KCYC(wait_out), KCYC(work), wcet_work);
    xil_printf("BENCH bcet_work=%u peak_occ=%u depth=%u\n", bcet_work, peak_occ, PIPE_DEPTH);
    xil_printf("BENCH_DONE\n");

    while (1) asm("wfi");
}
