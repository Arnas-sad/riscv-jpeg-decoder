/*
 * JPEG DATA-PARALLEL decoder (2b) - RISC-V driver.
 *
 * The same binary runs on every cooperating tile (TILE_ID 0..N_TILES-1, set by
 * the build from the partition_<TILE>_<PARTITION> directory name). VLD/entropy
 * decoding is inherently sequential (DC predictor + global Huffman bit buffer +
 * monotonically-advancing bitstream), so EVERY tile runs the full VLD over the
 * whole stream to keep that state correct - but a tile only performs the
 * expensive iqzz/idct/cc/emit for the MCUs it OWNS.
 *
 * Ownership is by output block, round-robin across tiles (see tile_emit.c).
 * Because the output address is derived from the global MCU index, the tiles'
 * out-of-order DMA writes self-sort into one shared scratch area; the ARM
 * scatter.c then blits the assembled tiles into the framebuffer. Each tile uses
 * its OWN DMA queue and OWN shared region (parallel.h), so no locks are needed.
 *
 * Benchmarking: per-actor partition-timer deltas are accumulated across all
 * MCUs and printed as BENCH_* lines at the end for parse_bench.py. Each tile
 * emits its own block; the readout channel prefix ("TT PP:") identifies the
 * tile. Absolute global timestamps (gstart/gend) let the parser prove the tiles
 * ran concurrently (overlapping intervals) and compute the true frame latency
 * as max(gend) - min(gstart). Never print inside the MCU loop - xil_printf is
 * ~4500 cycles per call and would dominate the measurement.
 *
 * Built incrementally from the sequential decoder (steps S1..S5); see
 * 1-sequential design doc. -DJPEG_DRAM_INPUT selects the windowed DRAM reader.
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
#include "parallel.h"

/* Safety cap so a runaway VLD prints instead of spinning forever. */
#define MAX_MCUS 200000

int main(void) {
    xil_printf("%d %d: hello from JPEG data-parallel decoder (tile %d/%d)\n",
               TILE_ID, PARTITION_ID, TILE_ID, N_TILES);

    /* --- S2: pull the TOC into local memory (own queue + own region). */
    volatile uint8_t *scratch = TILE_REGION->dma_scratch;
    dma_receive_from_dram(MY_DMA_QUEUE, TOC_START, scratch);
    while (dma_nr_pending_transactions(MY_DMA_QUEUE) > 0) asm("wfi");

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
    SubHeader1 sh1 = {0};
    SubHeader2 sh2 = {0};

    /* Per-actor ET accumulators (partition timer - actual execution cycles).
     * iqzz and idct are timed together because idct consumes iqzz's output, so
     * they can't be split without storing 10 intermediate FBlocks. VLD is timed
     * on every MCU (it runs on all tiles); iqzz/idct/cc/emit are timed only for
     * the MCUs THIS tile owns. */
    uint32_t et_vld       = 0, wcet_vld       = 0;
    uint32_t et_iqzz_idct = 0, wcet_iqzz_idct = 0;
    uint32_t et_cc        = 0, wcet_cc        = 0;
    uint32_t et_emit      = 0, wcet_emit      = 0;
    uint32_t t0, t1, delta;

    uint64_t t_start  = read_global_timer();      /* absolute: concurrency proof + rt */
    uint64_t pt_start = read_partition_timer();    /* execution-time base               */

    uint32_t gidx  = 0;     /* global MCU index, advances on every MCU       */
    uint32_t owned = 0;     /* MCUs this tile actually decoded               */
    int first       = 1;    /* first MCU overall (geometry not yet known)    */
    int first_owned = 1;    /* first MCU this tile owns (checksum signal)    */
    int aborted     = 0;

    for (;;) {
        /* VLD - the dataflow source actor; runs in full on every tile. */
        t0 = (uint32_t)read_partition_timer();
        int vld_ret = header_vld(&header, &header, fv, &sh1, &sh2);
        t1 = (uint32_t)read_partition_timer();
        delta = t1 - t0;
        et_vld += delta;
        if (delta > wcet_vld) wcet_vld = delta;
        if (vld_ret != 0) break;

        if (first) {
            /* Initialise the emitter now that we know the geometry. */
            tile_emit_init(sh2.x_size, sh2.y_size,
                           sh2.MCU_sx, sh2.MCU_sy, sh2.n_comp);
            xil_printf("%d %d: geometry x=%d y=%d MCU=%dx%d n_comp=%d "
                       "total_mcus=%u\n",
                       TILE_ID, PARTITION_ID, sh2.x_size, sh2.y_size,
                       sh2.MCU_sx, sh2.MCU_sy, sh2.n_comp, tile_emit_total());
            first = 0;
            /* Framebuffer too big for this platform: every tile agrees, stop. */
            if (tile_emit_failed()) break;
        }

        if (tile_emit_owns(gidx)) {
            /* S4 compute: IQZZ + IDCT (timed together, see note above). */
            t0 = (uint32_t)read_partition_timer();
            for (int i = 0; i < 10; i++) {
                iqzz(&fv[i], &fb);
                idct(&fb, &pb[i]);
            }
            t1 = (uint32_t)read_partition_timer();
            delta = t1 - t0;
            et_iqzz_idct += delta;
            if (delta > wcet_iqzz_idct) wcet_iqzz_idct = delta;

            /* Colour conversion. */
            t0 = (uint32_t)read_partition_timer();
            cc(&sh1, pb, &cb);
            t1 = (uint32_t)read_partition_timer();
            delta = t1 - t0;
            et_cc += delta;
            if (delta > wcet_cc) wcet_cc = delta;

            if (first_owned) {
                /* Checksum this tile's first owned MCU: a quick per-tile
                   correctness signal against the host reference decoder. */
                int nbytes = (sh2.n_comp == 1)
                           ? (sh2.MCU_sx * sh2.MCU_sy)
                           : (3 * sh2.MCU_sx * sh2.MCU_sy);
                uint32_t sum = 0;
                for (int k = 0; k < nbytes; k++) sum += cb.data[k];
                xil_printf("%d %d: first owned MCU idx=%u checksum=%u "
                           "first_bgr=%d %d %d\n",
                           TILE_ID, PARTITION_ID, gidx, sum,
                           (int)cb.data[0], (int)cb.data[1], (int)cb.data[2]);
                first_owned = 0;
            }

            /* S5c: rasterise tile-local and DMA the tile (includes DMA wait). */
            t0 = (uint32_t)read_partition_timer();
            tile_emit_mcu(&sh2, &cb, gidx);
            t1 = (uint32_t)read_partition_timer();
            delta = t1 - t0;
            et_emit += delta;
            if (delta > wcet_emit) wcet_emit = delta;

            owned++;
            if (tile_emit_failed()) break;   /* scratch exhausted */
        }

        gidx++;
        if (gidx >= MAX_MCUS) { aborted = 1; break; }
    }

    /* Publish the geometry header (scratch block 0) for the gather pass. */
    tile_emit_finish();

    /* --- Done-barrier + RISC-V gather (Track A) ------------------------------
     * Under the self_check grader no ARM step runs: the framebuffer must be
     * fully composed by RISC-V before we signal done. Each tile announces it
     * has flushed all its scratch tiles; tile 0 waits for ALL tiles (so no FB
     * block is composed from tiles another tile hasn't written yet), then it
     * alone composes the framebuffer from every tile's scratch tiles, and
     * finally zeroes the TOC done-word that `dram.sh poll` watches. The other
     * tiles have nothing left to do and fall through to their idle wfi.
     * Ordering matters: gather (framebuffer ready) strictly before the
     * done-signal - that signal IS the WCRT endpoint. */
    vep_memshared0_shared_region->done[TILE_ID] = 1;
    if (TILE_ID == 0) {
        for (int t = 0; t < N_TILES; t++)
            while (vep_memshared0_shared_region->done[t] == 0) asm("wfi");
        if (!tile_emit_failed()) {
            fb_gather(MY_DMA_QUEUE,
                      TILE_REGION->input_block, TILE_REGION->tile_buf);
            fb_signal_done(MY_DMA_QUEUE, TILE_REGION->dma_scratch);
        }
    }

    uint64_t pt_end = read_partition_timer();
    uint64_t t_end  = read_global_timer();
    uint32_t rt_cycles = (uint32_t)(t_end  - t_start);   /* wall-clock (incl. wait) */
    uint32_t et_cycles = (uint32_t)(pt_end - pt_start);  /* executed cycles         */

    xil_printf("%d %d: scanned mcus=%u owned=%u bytes=%u of %u  "
               "rt=%u (%u ms) et=%u (%u ms)\n",
               TILE_ID, PARTITION_ID, gidx, owned,
               (uint32_t)header.fp.vld_count, jpeg_size,
               rt_cycles, rt_cycles / (FREQUENCY / 1000),
               et_cycles, et_cycles / (FREQUENCY / 1000));

    int fail = tile_emit_failed();

    /* Structured benchmark output - parsed by parse_bench.py. One BENCH block
     * per tile, only on a clean decode. NO "TT PP:" prefix on BENCH lines: the
     * readout adds the channel prefix, which is how the parser groups by tile.
     * Image name is injected by run_benchmarks.sh (--image). */
    if (!aborted && fail == 0) {
        xil_printf("BENCH impl=data-parallel tile=%d n_tiles=%d owned=%u\n",
                   TILE_ID, N_TILES, owned);
        xil_printf("BENCH n_mcus=%u xsize=%d ysize=%d mcu_sx=%d mcu_sy=%d "
                   "n_comp=%d jpeg_size=%u\n",
                   tile_emit_total(), sh2.x_size, sh2.y_size,
                   sh2.MCU_sx, sh2.MCU_sy, sh2.n_comp, jpeg_size);
        xil_printf("BENCH rt_cycles=%u rt_ms=%u et_cycles=%u et_ms=%u\n",
                   rt_cycles, rt_cycles / (FREQUENCY / 1000),
                   et_cycles, et_cycles / (FREQUENCY / 1000));
        /* Absolute 64-bit global timestamps, split hi/lo (xil_printf is 32-bit).
         * Parser rebuilds gstart=hi<<32|lo to prove overlap across tiles. */
        xil_printf("BENCH gstart_hi=%u gstart_lo=%u gend_hi=%u gend_lo=%u\n",
                   (uint32_t)(t_start >> 32), (uint32_t)t_start,
                   (uint32_t)(t_end   >> 32), (uint32_t)t_end);
        xil_printf("BENCH et_vld=%u et_iqzz_idct=%u et_cc=%u et_emit=%u\n",
                   et_vld, et_iqzz_idct, et_cc, et_emit);
        xil_printf("BENCH wcet_vld=%u wcet_iqzz_idct=%u wcet_cc=%u wcet_emit=%u\n",
                   wcet_vld, wcet_iqzz_idct, wcet_cc, wcet_emit);
        xil_printf("BENCH_DONE\n");
    }

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
        xil_printf("%d %d: tile done (owned %u of %u MCUs)\n",
                   TILE_ID, PARTITION_ID, owned, tile_emit_total());
        if (TILE_ID == 0)
            xil_printf("%d %d: S5 done. Framebuffer composed on RISC-V and TOC "
                       "done-word cleared. Read it out with: "
                       "./dram.sh bmp width %d height %d\n",
                       TILE_ID, PARTITION_ID, sh2.x_size, sh2.y_size);
    }

    while (1) asm("wfi");
}
