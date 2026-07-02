/*
 * JPEG sequential decoder - RISC-V driver (tile 0 partition 1).
 *
 * Built incrementally (see 1-sequential design doc, steps S1..S5):
 *   S2: read the TOC from DRAM via DMA.
 *   S3: windowed DRAM reader behind FGETC; run the VLD/header loop.
 *   S4/S5 (this file): full pipeline.
 *     S4 compute - per MCU: iqzz -> idct (x10 data units) -> cc -> ColorBuffer.
 *                  The first MCU's colour buffer is checksummed for a quick
 *                  correctness signal against the host reference decoder.
 *     S5c output - each MCU is rasterised tile-local and DMAed to a DRAM
 *                  scratch area (tile_emit.c). The RISC-V never dereferences
 *                  the framebuffer directly (that would fault).
 *     Gather     - after decode, fb_gather composes the framebuffer from the
 *                  staged tiles via DMA (one write per 1 KiB FB block) and
 *                  fb_signal_done zeroes TOC word 0 - the done-signal that
 *                  `dram.sh poll` watches. No ARM step; `dram.sh bmp` reads
 *                  the finished framebuffer. (scatter.c remains as a debug
 *                  oracle only.)
 */
#include <stdint.h>
#include <timers.h>
#include <xil_printf.h>
#include <platform.h>
#include <dma.h>
#include <vep_shared_memory_regions.h>
#include "structures.h"
#include "actors.h"
#include "dram_input.h"
#include "tile_emit.h"
#include "fb_gather.h"

/* Safety cap so a runaway VLD prints instead of spinning forever. */
#define MAX_MCUS 200000

int main(void) {
    xil_printf("%d %d: hello from JPEG sequential decoder (S4/S5)\n",
               TILE_ID, PARTITION_ID);

    /* --- S2: pull the TOC into local memory. */
    volatile uint8_t *scratch = vep_tile0_partition1_shared_region->dma_scratch;
    dma_receive_from_dram(DMA_QUEUE_0, TOC_START, scratch);
    while (dma_nr_pending_transactions(DMA_QUEUE_0) > 0) asm("wfi");

    volatile uint32_t *toc = (volatile uint32_t *)scratch;
    uint32_t jpeg_addr = toc[2];
    uint32_t jpeg_size = toc[3];
    xil_printf("%d %d: TOC jpeg_addr=0x%08X jpeg_size=%u\n",
               TILE_ID, PARTITION_ID, jpeg_addr, jpeg_size);

    /* --- S3: route FGETC through the windowed DRAM reader. */
    dram_input_init(jpeg_addr);

    static VldHeader header;
    init_header_vld(&header, (const unsigned int *)jpeg_addr,
                    (unsigned int *)FRAME_BUFFER_START);

    /* Per-MCU working storage (static: too big for the 8 KiB stack). */
    static FValue      fv[10];
    static FBlock      fb;
    static PBlock      pb[10];
    static ColorBuffer cb;
    SubHeader1 sh1;
    SubHeader2 sh2;

    uint64_t t_start = read_global_timer();

    uint32_t mcus = 0;
    int first = 1;
    int aborted = 0;

    while (header_vld(&header, &header, fv, &sh1, &sh2) == 0) {
        /* S4 compute: 10 data units -> de-quant/un-zigzag -> inverse DCT. */
        for (int i = 0; i < 10; i++) {
            iqzz(&fv[i], &fb);
            idct(&fb, &pb[i]);
        }
        /* Colour-convert the whole MCU into the BGR(A source) colour buffer. */
        cc(&sh1, pb, &cb);

        if (first) {
            /* Initialise the emitter now that we know the geometry. */
            tile_emit_init(sh2.x_size, sh2.y_size,
                           sh2.MCU_sx, sh2.MCU_sy, sh2.n_comp);

            /* Checksum the first MCU's colour buffer: compare against the host
               reference decoder to confirm the compute path is bit-correct. */
            int nbytes = (sh2.n_comp == 1)
                       ? (sh2.MCU_sx * sh2.MCU_sy)
                       : (3 * sh2.MCU_sx * sh2.MCU_sy);
            uint32_t sum = 0;
            for (int k = 0; k < nbytes; k++) sum += cb.data[k];
            xil_printf("%d %d: S4 first-MCU checksum=%u first_bgr=%d %d %d\n",
                       TILE_ID, PARTITION_ID, sum,
                       (int)cb.data[0], (int)cb.data[1], (int)cb.data[2]);
            xil_printf("%d %d: geometry x=%d y=%d MCU=%dx%d n_comp=%d\n",
                       TILE_ID, PARTITION_ID, sh2.x_size, sh2.y_size,
                       sh2.MCU_sx, sh2.MCU_sy, sh2.n_comp);
            first = 0;
        }

        /* S5c: rasterise tile-local and DMA the tile to the scratch area. */
        tile_emit_mcu(&sh2, &cb);

        mcus++;
        if (mcus >= MAX_MCUS) { aborted = 1; break; }
    }

    /* Publish the geometry header (scratch block 0) for the gather pass. */
    tile_emit_finish();

    /* RISC-V gather: compose the framebuffer from the staged scratch tiles,
     * then signal done by zeroing the TOC word `dram.sh poll` watches.
     * Ordering matters: gather (framebuffer ready) strictly before the
     * done-signal. Inside the timed window so `cycles` covers the full
     * input-ready -> output-ready span. */
    if (!aborted && !tile_emit_failed()) {
        fb_gather(DMA_QUEUE_0,
                  vep_tile0_partition1_shared_region->input_block,
                  vep_tile0_partition1_shared_region->tile_buf);
        fb_signal_done(DMA_QUEUE_0,
                       vep_tile0_partition1_shared_region->dma_scratch);
    }

    uint64_t t_end  = read_global_timer();
    uint32_t cycles = (uint32_t)(t_end - t_start);
    xil_printf("%d %d: decoded mcus=%u bytes=%u of %u  cycles=%u (%u ms)\n",
               TILE_ID, PARTITION_ID, mcus, (uint32_t)header.fp.vld_count,
               jpeg_size, cycles, cycles / (FREQUENCY / 1000));

    int fail = tile_emit_failed();
    if (aborted) {
        xil_printf("%d %d: S5 ABORT - VLD did not reach end-of-image\n",
                   TILE_ID, PARTITION_ID);
    } else if (fail == 1) {
        xil_printf("%d %d: OUT OF SCOPE - %dx%d framebuffer exceeds the 64 MiB "
                   "region; this platform cannot hold this image\n",
                   TILE_ID, PARTITION_ID, sh2.x_size, sh2.y_size);
    } else if (fail == 2) {
        xil_printf("%d %d: OUT OF SCOPE - %dx%d needs more tile scratch than "
                   "DRAM has; image too large\n",
                   TILE_ID, PARTITION_ID, sh2.x_size, sh2.y_size);
    } else {
        xil_printf("%d %d: S5 done. Framebuffer composed on RISC-V and TOC "
                   "done-word cleared. Read it out with: "
                   "./dram.sh bmp width %d height %d\n",
                   TILE_ID, PARTITION_ID, sh2.x_size, sh2.y_size);
    }

    while (1) asm("wfi");
}
