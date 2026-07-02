#ifndef _JPEG_FB_GATHER_H_INCLUDED
#define _JPEG_FB_GATHER_H_INCLUDED

#include <stdint.h>
#include <dma.h>

/*
 * Post-decode framebuffer GATHER pass - RISC-V side (2c function-parallel sink).
 *
 * The raster sink stages every decoded MCU as a packed tile in a DRAM scratch
 * area (see tile_emit.h). Under the self_check grader no ARM step runs: the
 * framebuffer must be composed entirely by RISC-V before the decoder signals
 * done. This module does that final composition - the destination-driven inverse
 * of scatter.c: walk the framebuffer one 1 KiB block at a time, DMA-read only the
 * scratch blocks covering that block, compose locally, DMA-write it once.
 *
 * The sink is a single partition (alone on tile 2), so no done-barrier is needed:
 * once tile_emit_finish() returns the scratch is complete and it gathers.
 *
 * It is also the WCRT endpoint: fb_signal_done() zeroing the TOC word is exactly
 * "the RISC-V partition signals the output frame is ready" (spec p29).
 */

/* Compose the whole framebuffer from the packed MCU tiles in DRAM scratch.
   Geometry is read from scratch block 0 (the header tile_emit_finish wrote).
   cache_buf and out_buf must each be a distinct 1 KiB-aligned DMA-capable
   shared-region buffer (1024 bytes). Returns 0 ok, -1 if header missing. */
int  fb_gather      (TileQueue_t dma_queue,
                     volatile uint8_t *cache_buf,
                     volatile uint8_t *out_buf);

/* Done-signal watched by `dram.sh poll`: zero word 0 of the DRAM TOC (RMW so
   words 1..7 survive). MUST be called STRICTLY AFTER fb_gather returns. buf is a
   1 KiB-aligned DMA-capable shared-region buffer (1024 bytes). */
void fb_signal_done (TileQueue_t dma_queue, volatile uint8_t *buf);

#endif /* _JPEG_FB_GATHER_H_INCLUDED */
