#ifndef _JPEG_FB_GATHER_H_INCLUDED
#define _JPEG_FB_GATHER_H_INCLUDED

#include <stdint.h>
#include <dma.h>

/*
 * Post-decode framebuffer GATHER pass - RISC-V side (Track A, 2b/2c/2d).
 *
 * The decoder stages every decoded MCU as a packed tile in a DRAM scratch area
 * (see tile_emit.h). Historically an ARM helper (scatter.c) blitted those tiles
 * into the framebuffer. Under the self_check grading script no ARM step ever
 * runs: the framebuffer must be complete, composed entirely by RISC-V, before
 * the decoder signals "done". This module does that final composition.
 *
 * It is the destination-driven inverse of scatter.c: instead of walking MCUs
 * and scattering each into the framebuffer (which would need a read-modify-write
 * of up to 8 FB blocks per MCU, ~20x runtime), it walks the framebuffer one
 * 1 KiB block at a time, DMA-reads only the scratch blocks covering that block's
 * pixels (caching one at a time - the VLD-ordered tiles are read left-to-right,
 * so the cache reloads only a handful of times per FB block), composes the full
 * 1 KiB locally, and DMA-writes it once. Cost is ~2-3% of decode time.
 *
 * It is version-agnostic: the barrier that guarantees all data-parallel tiles
 * have finished flushing lives in the caller (main.c), not here.
 */

/* Compose the whole framebuffer from the packed MCU tiles in DRAM scratch.
   Geometry is read from scratch block 0 (the header tile_emit_finish wrote), so
   this needs no decoder state. cache_buf and out_buf must each be a distinct,
   1 KiB-aligned, DMA-capable shared-region buffer (1024 bytes).
   Returns 0 on success, -1 if the scratch header is missing or degenerate
   (e.g. the decoder bailed out: framebuffer too large). */
int  fb_gather      (TileQueue_t dma_queue,
                     volatile uint8_t *cache_buf,
                     volatile uint8_t *out_buf);

/* Done-signal watched by `dram.sh poll`: zero word 0 of the DRAM TOC. Done as a
   read-modify-write so words 1..7 of the TOC are preserved. MUST be called
   STRICTLY AFTER the framebuffer is complete (after fb_gather returns) - this is
   the instant the grader snapshots the framebuffer. buf is a 1 KiB-aligned,
   DMA-capable shared-region buffer (1024 bytes). */
void fb_signal_done (TileQueue_t dma_queue, volatile uint8_t *buf);

#endif /* _JPEG_FB_GATHER_H_INCLUDED */
