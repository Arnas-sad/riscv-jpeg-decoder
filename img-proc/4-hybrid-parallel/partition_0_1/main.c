/*
 * JPEG HYBRID-PARALLEL decoder (2d) - VLD SOURCE (tile 0, partition 1).
 *
 * Entropy-decode one MCU (VLD, stateful - DC predictor + bit buffer = the
 * actor's self-edge) and emit ONE coefficient token ROUND-ROBIN to the two
 * IQZZ+IDCT workers on tile 1 (even MCU -> A, odd -> B). IQZZ was MOVED to the
 * workers so this single DMA-in tile carries VLD only (it was the bottleneck);
 * each worker fuses IQZZ+IDCT locally (data-split). At EOI a `last` sentinel
 * goes to BOTH branches. S-02: only nr_data_units fv blocks cross the ring.
 */
#include <stdint.h>
#include <string.h>
#include <timers.h>
#include <xil_printf.h>
#include <platform.h>
#include <dma.h>
#include <fifo.h>
#include <vep_shared_memory_regions.h>
#include "structures.h"
#include "actors.h"
#include "dram_input.h"
#include "parallel.h"
#include "pipeline.h"

#define MAX_MCUS 200000
#define MEM      vep_memshared0_shared_region
#define KCYC(x)  ((uint32_t)((x) >> 10))

int main(void) {
    xil_printf("%d %d: 2d VLD source starting\n", TILE_ID, PARTITION_ID);

    _Static_assert(sizeof(tok_coef_t) <= TOK_COEF_SIZE,
                   "tok_coef_t larger than TOK_COEF_SIZE");

    /* --- TOC: locate the uploaded JPEG. */
    volatile uint8_t *scratch = TILE_REGION->dma_scratch;
    dma_receive_from_dram(MY_DMA_QUEUE, TOC_START, scratch);
    while (dma_nr_pending_transactions(MY_DMA_QUEUE) > 0) asm("wfi");
    volatile uint32_t *toc = (volatile uint32_t *)scratch;
    uint32_t jpeg_addr = toc[2];
    uint32_t jpeg_size = toc[3];
    xil_printf("%d %d: TOC jpeg_addr=0x%08X jpeg_size=%u\n",
               TILE_ID, PARTITION_ID, jpeg_addr, jpeg_size);

    dram_input_init(jpeg_addr);

    static VldHeader header;
    init_header_vld(&header, (const unsigned int *)jpeg_addr,
                    (unsigned int *)FRAME_BUFFER_START);

    /* Bring up both branch FIFOs; the workers wait on fifo_initialized. */
    fifo_init(&MEM->f_freq_a, MEM->b_freq_a, PIPE_DEPTH, sizeof(tok_coef_t));
    fifo_init(&MEM->f_freq_b, MEM->b_freq_b, PIPE_DEPTH, sizeof(tok_coef_t));

    static FValue fv[10];
    SubHeader1 sh1;
    SubHeader2 sh2;

    uint64_t work = 0, wait_out = 0;
    uint32_t wcet_work = 0;
    uint32_t bcet_work = 0xFFFFFFFFu, peak_occ = 0;
    uint64_t bytes_out = 0;
    uint64_t t0, t1, t2, t3;
    uint32_t firing;

    uint64_t g_start  = read_global_timer();
    uint64_t pt_start = read_partition_timer();
    uint32_t mcus = 0;

    for (;;) {
        /* VLD only - IQZZ now runs in the workers. */
        t0 = read_partition_timer();
        int vld_ret = header_vld(&header, &header, fv, &sh1, &sh2);
        t1 = read_partition_timer();
        work += t1 - t0;
        if (vld_ret != 0) break;

        /* Round-robin: even MCU -> branch A, odd -> branch B. */
        fifo_t volatile *out_f = (mcus & 1u) ? &MEM->f_freq_b : &MEM->f_freq_a;
        tok_coef_t volatile *out =
            (tok_coef_t volatile *)fifo_claim_space(out_f);   /* blocking */
        t2 = read_partition_timer();
        wait_out += t2 - t1;

        out->last = 0;
        out->sh1  = sh1;
        out->sh2  = sh2;
        out->nr_data_units = header.nr_data_units;   /* worker loop bound */
        /* S-02: only the valid blocks cross the ring (the unused fv[] tail is
           never read by the worker). */
        memcpy((void *)out->fv, fv,
               (size_t)header.nr_data_units * sizeof(FValue));
        fifo_release_token(out_f);
        t3 = read_partition_timer();
        work += t3 - t2;

        firing = (uint32_t)((t1 - t0) + (t3 - t2));
        if (firing > wcet_work) wcet_work = firing;
        if (firing < bcet_work) bcet_work = firing;
        bytes_out += (sizeof(tok_coef_t) - 10u * sizeof(FValue))
                   + (uint64_t)header.nr_data_units * sizeof(FValue);
        { uint32_t o = fifo_tokens(out_f); if (o > peak_occ) peak_occ = o; }

        if (++mcus >= MAX_MCUS) break;
    }

    /* End-of-stream sentinel to BOTH branches so the whole pipeline drains. */
    tok_coef_t volatile *eos;
    eos = (tok_coef_t volatile *)fifo_claim_space(&MEM->f_freq_a);
    eos->last = 1;
    fifo_release_token(&MEM->f_freq_a);
    eos = (tok_coef_t volatile *)fifo_claim_space(&MEM->f_freq_b);
    eos->last = 1;
    fifo_release_token(&MEM->f_freq_b);

    uint64_t pt_end = read_partition_timer();
    uint64_t g_end  = read_global_timer();
    uint64_t rt = g_end - g_start, et = pt_end - pt_start;

    xil_printf("%d %d: VLD done - emitted %u MCUs in %u ms\n",
               TILE_ID, PARTITION_ID, mcus, (uint32_t)(rt / (FREQUENCY / 1000)));

    xil_printf("BENCH impl=hybrid-parallel stage=vld_iqzz tile=%d partition=%d n_mcus=%u jpeg_size=%u\n",
               TILE_ID, PARTITION_ID, mcus, jpeg_size);
    xil_printf("BENCH xsize=%d ysize=%d mcu_sx=%d mcu_sy=%d n_comp=%d\n",
               header.x_size, header.y_size, header.MCU_sx, header.MCU_sy,
               header.n_comp);
    xil_printf("BENCH rt_kcyc=%u rt_ms=%u et_kcyc=%u et_ms=%u\n",
               KCYC(rt), (uint32_t)(rt / (FREQUENCY / 1000)),
               KCYC(et), (uint32_t)(et / (FREQUENCY / 1000)));
    xil_printf("BENCH gstart_hi=%u gstart_lo=%u gend_hi=%u gend_lo=%u\n",
               (uint32_t)(g_start >> 32), (uint32_t)g_start,
               (uint32_t)(g_end   >> 32), (uint32_t)g_end);
    xil_printf("BENCH wait_in_kcyc=0 wait_out_kcyc=%u work_kcyc=%u wcet_work=%u\n",
               KCYC(wait_out), KCYC(work), wcet_work);
    xil_printf("BENCH bcet_work=%u peak_occ=%u depth=%u bytes_out_kib=%u\n",
               bcet_work, peak_occ, PIPE_DEPTH, (uint32_t)(bytes_out >> 10));
    xil_printf("BENCH_DONE\n");

    while (1) asm("wfi");
}
