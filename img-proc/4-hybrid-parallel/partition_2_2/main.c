/*
 * JPEG HYBRID-PARALLEL decoder (2d) - CC+RASTER SINK (tile 2, partition 2).
 *
 * The merged back-end actor: reads pixel tokens from the two IDCT workers by
 * STRICT ALTERNATION (A, B, A, B, ... mirroring the source's round-robin
 * deal), which reconstructs raster order exactly - so the append-counter
 * output path and the ARM scatter stay unchanged. For each MCU: colour-convert
 * (CC), rasterise tile-local and DMA to the DRAM scratch (tile_emit).
 *
 * A branch is finished when its `last` sentinel arrives; the sink keeps
 * draining the other branch until both are done (an odd MCU count means A
 * carries one more token than B). Alone on tile 2 - it owns the DMA-out
 * region (platform sibling rule).
 *
 * Benchmarking: same BENCH protocol as 2c (wait_in vs work; work includes CC,
 * raster and the DMA wait).
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
#include "tile_emit.h"
#include "fb_gather.h"

#define MEM            vep_memshared0_shared_region
#define RASTER_REGION  vep_tile2_partition2_shared_region
#define RASTER_QUEUE   DMA_QUEUE_2
#define KCYC(x)        ((uint32_t)((x) >> 10))

int main(void) {
    xil_printf("%d %d: 2d CC+raster sink starting\n", TILE_ID, PARTITION_ID);

    while (!fifo_initialized(&MEM->f_pix_a)) asm("wfi");
    while (!fifo_initialized(&MEM->f_pix_b)) asm("wfi");

    static PBlock      pbloc[10];
    static ColorBuffer cbloc;
    SubHeader1 sh1 = {0};
    SubHeader2 sh2 = {0};

    uint64_t work = 0, wait_in = 0;
    uint32_t wcet_work = 0;
    uint32_t bcet_work = 0xFFFFFFFFu, peak_occ = 0, gather_kcyc = 0;
    uint64_t t0, t1, t2;
    uint32_t firing;
    uint32_t n = 0;
    int first  = 1;
    int done_a = 0, done_b = 0;
    int cur    = 0;                     /* 0 = branch A next, 1 = branch B    */

    uint64_t g_start  = read_global_timer();
    uint64_t pt_start = read_partition_timer();

    while (!done_a || !done_b) {
        /* Strict alternation, skipping a branch once its sentinel arrived. */
        if ((cur == 0 && done_a) || (cur == 1 && done_b)) { cur ^= 1; continue; }
        fifo_t volatile *f = cur ? &MEM->f_pix_b : &MEM->f_pix_a;

        t0 = read_partition_timer();
        tok_pix_t volatile *in = (tok_pix_t volatile *)fifo_claim_token(f);
        t1 = read_partition_timer();
        wait_in += t1 - t0;

        int last = in->last;
        if (!last) {
            sh1 = in->sh1; sh2 = in->sh2;
            memcpy(pbloc, (const void *)in->pb, sizeof(pbloc));
        }
        fifo_release_space(f);          /* always drain so upstream never stalls */

        if (last) {
            if (cur) done_b = 1; else done_a = 1;
            cur ^= 1;
            continue;
        }

        if (first) {
            tile_emit_init(sh2.x_size, sh2.y_size, sh2.MCU_sx, sh2.MCU_sy, sh2.n_comp);
            first = 0;
        }

        /* CC + raster + DMA = this actor's work. On out-of-scope keep draining
           tokens but stop emitting, so the pipeline still terminates. */
        if (!tile_emit_failed()) {
            cc(&sh1, pbloc, &cbloc);
            if (n == 0) {
                /* End-to-end correctness signal - must be 74240 for cat.jpg. */
                int nbytes = (sh2.n_comp == 1)
                           ? (sh2.MCU_sx * sh2.MCU_sy)
                           : (3 * sh2.MCU_sx * sh2.MCU_sy);
                uint32_t sum = 0;
                for (int k = 0; k < nbytes; k++) sum += cbloc.data[k];
                xil_printf("%d %d: geometry x=%d y=%d MCU=%dx%d n_comp=%d "
                           "first-MCU checksum=%u (expect cat=74240)\n",
                           TILE_ID, PARTITION_ID, sh2.x_size, sh2.y_size,
                           sh2.MCU_sx, sh2.MCU_sy, sh2.n_comp, sum);
            }
            tile_emit_mcu(&sh2, &cbloc);
            n++;
        }

        t2 = read_partition_timer();
        work += t2 - t1;
        firing = (uint32_t)(t2 - t1);
        if (firing > wcet_work) wcet_work = firing;
        if (firing < bcet_work) bcet_work = firing;
        { uint32_t o = fifo_tokens(f); if (o > peak_occ) peak_occ = o; }

        cur ^= 1;                       /* alternate branches */
    }

    tile_emit_finish();

    /* RISC-V gather: compose the framebuffer from the staged scratch tiles, then
     * signal done by zeroing the TOC word `dram.sh poll` watches. No barrier -
     * the sink is alone on its tile, so the scratch is complete here. Done before
     * the g_end read so the sink's RT covers composition, and the done-signal
     * (the WCRT endpoint) is strictly the last DMA. */
    if (!tile_emit_failed()) {
        uint64_t gs = read_partition_timer();
        fb_gather(RASTER_QUEUE, RASTER_REGION->input_block, RASTER_REGION->tile_buf);
        gather_kcyc = KCYC(read_partition_timer() - gs);
        fb_signal_done(RASTER_QUEUE, RASTER_REGION->dma_scratch);
    }

    uint64_t pt_end = read_partition_timer();
    uint64_t g_end  = read_global_timer();
    uint64_t rt = g_end - g_start, et = pt_end - pt_start;
    int fail = tile_emit_failed();

    xil_printf("%d %d: CC+raster done - %u MCUs in %u ms\n",
               TILE_ID, PARTITION_ID, n, (uint32_t)(rt / (FREQUENCY / 1000)));

    if (fail == 0) {
        xil_printf("BENCH impl=hybrid-parallel stage=cc_raster tile=%d partition=%d n_mcus=%u jpeg_size=0\n",
                   TILE_ID, PARTITION_ID, n);
        xil_printf("BENCH xsize=%d ysize=%d mcu_sx=%d mcu_sy=%d n_comp=%d\n",
                   sh2.x_size, sh2.y_size, sh2.MCU_sx, sh2.MCU_sy, sh2.n_comp);
        xil_printf("BENCH rt_kcyc=%u rt_ms=%u et_kcyc=%u et_ms=%u\n",
                   KCYC(rt), (uint32_t)(rt / (FREQUENCY / 1000)),
                   KCYC(et), (uint32_t)(et / (FREQUENCY / 1000)));
        xil_printf("BENCH gstart_hi=%u gstart_lo=%u gend_hi=%u gend_lo=%u\n",
                   (uint32_t)(g_start >> 32), (uint32_t)g_start,
                   (uint32_t)(g_end   >> 32), (uint32_t)g_end);
        xil_printf("BENCH wait_in_kcyc=%u wait_out_kcyc=0 work_kcyc=%u wcet_work=%u\n",
                   KCYC(wait_in), KCYC(work), wcet_work);
        xil_printf("BENCH bcet_work=%u peak_occ=%u depth=%u gather_kcyc=%u\n",
                   bcet_work, peak_occ, PIPE_DEPTH, gather_kcyc);
        xil_printf("BENCH_DONE\n");
    }

    if (fail == 1)
        xil_printf("%d %d: OUT OF SCOPE - %dx%d framebuffer exceeds 64 MiB\n",
                   TILE_ID, PARTITION_ID, sh2.x_size, sh2.y_size);
    else if (fail == 2)
        xil_printf("%d %d: OUT OF SCOPE - scratch exhausted\n", TILE_ID, PARTITION_ID);
    else
        xil_printf("%d %d: S5 done. Framebuffer composed on RISC-V and TOC "
                   "done-word cleared. Read it out with: "
                   "./dram.sh bmp width %d height %d\n",
                   TILE_ID, PARTITION_ID, sh2.x_size, sh2.y_size);

    while (1) asm("wfi");
}
