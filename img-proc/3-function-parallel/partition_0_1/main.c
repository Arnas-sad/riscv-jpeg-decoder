/*
 * JPEG FUNCTION-PARALLEL decoder (2c) - VLD SOURCE actor (tile 0, partition 1).
 *
 * Pipeline source: reads the JPEG from DRAM (windowed reader), runs the entropy
 * decode, and emits one coefficient token per MCU into f_coef. VLD keeps its
 * own state (DC predictor, Huffman bit buffer) locally between firings - the
 * actor's self-edge in dataflow terms.
 *
 * Benchmarking: accumulates work (decode + token copy) vs wait_out (blocked on
 * FIFO space = downstream backpressure) on the partition timer, and prints one
 * BENCH block at the end (never inside the loop - xil_printf is ~4500 cycles).
 * Totals are 64-bit and printed in kibicycles (>>10): big images exceed 2^32
 * cycles. parse_bench.py groups blocks by the readout's "TT PP:" prefix.
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
    xil_printf("%d %d: 2c VLD source starting\n", TILE_ID, PARTITION_ID);

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

    fifo_t *coef = fifo_init(&MEM->f_coef, MEM->b_coef,
                             PIPE_DEPTH, sizeof(tok_coef_t));
    if (coef == NULL) {
        xil_printf("%d %d: ERROR fifo_init failed\n", TILE_ID, PARTITION_ID);
        while (1) asm("wfi");
    }

    static FValue fv[10];
    SubHeader1 sh1;
    SubHeader2 sh2;

    uint64_t work = 0, wait_out = 0;   /* partition-timer buckets            */
    uint32_t wcet_work = 0;            /* worst single firing (decode+copy)  */
    uint32_t bcet_work = 0xFFFFFFFFu;  /* best single firing (ET range)      */
    uint64_t bytes_out = 0;            /* B5: ring bytes into f_coef (S-02 trimmed) */
    uint32_t peak_occ  = 0;            /* B4: max f_coef occupancy (depth proof)    */
    uint64_t t0, t1, t2, t3;
    uint32_t firing;

    uint64_t g_start  = read_global_timer();
    uint64_t pt_start = read_partition_timer();
    uint32_t mcus = 0;

    for (;;) {
        t0 = read_partition_timer();
        int vld_ret = header_vld(&header, &header, fv, &sh1, &sh2);
        t1 = read_partition_timer();
        work += t1 - t0;
        if (vld_ret != 0) break;

        tok_coef_t volatile *out =
            (tok_coef_t volatile *)fifo_claim_space(&MEM->f_coef);   /* blocking */
        t2 = read_partition_timer();
        wait_out += t2 - t1;

        out->last = 0;
        out->sh1  = sh1;
        out->sh2  = sh2;
        out->nr_data_units = header.nr_data_units;   /* S-01: real blocks/MCU */
        /* S-02: copy only the valid blocks - token writes go word-by-word over
           the memory ring, so the unused fv[] tail is pure wasted traffic. */
        memcpy((void *)out->fv, fv,
               (size_t)header.nr_data_units * sizeof(FValue));
        fifo_release_token(&MEM->f_coef);
        t3 = read_partition_timer();
        work += t3 - t2;

        firing = (uint32_t)((t1 - t0) + (t3 - t2));
        if (firing > wcet_work) wcet_work = firing;
        if (firing < bcet_work) bcet_work = firing;

        /* Sampled OUTSIDE the work timer so work_kcyc stays comparable. */
        /* B5 comm volume: only the fixed header fields + nr_data_units fv blocks
           cross the ring (S-02), never the unused fv[10] tail. */
        bytes_out += (sizeof(tok_coef_t) - 10u * sizeof(FValue))
                   + (uint64_t)header.nr_data_units * sizeof(FValue);
        /* B4 peak occupancy of the output FIFO (= how full depth-PIPE_DEPTH got). */
        { uint32_t occ = fifo_tokens(&MEM->f_coef);
          if (occ > peak_occ) peak_occ = occ; }

        if (++mcus >= MAX_MCUS) break;
    }

    /* End-of-stream sentinel so the pipeline drains and stops. */
    tok_coef_t volatile *eos = (tok_coef_t volatile *)fifo_claim_space(&MEM->f_coef);
    eos->last = 1;
    fifo_release_token(&MEM->f_coef);

    uint64_t pt_end = read_partition_timer();
    uint64_t g_end  = read_global_timer();
    uint64_t rt = g_end - g_start, et = pt_end - pt_start;

    xil_printf("%d %d: VLD done - emitted %u MCUs in %u ms\n",
               TILE_ID, PARTITION_ID, mcus, (uint32_t)(rt / (FREQUENCY / 1000)));

    xil_printf("BENCH impl=function-parallel stage=vld tile=%d partition=%d n_mcus=%u jpeg_size=%u\n",
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
    xil_printf("BENCH bcet_work=%u bytes_out_kib=%u peak_occ=%u depth=%u\n",
               bcet_work, (uint32_t)(bytes_out >> 10), peak_occ, PIPE_DEPTH);
    xil_printf("BENCH_DONE\n");

    while (1) asm("wfi");
}
