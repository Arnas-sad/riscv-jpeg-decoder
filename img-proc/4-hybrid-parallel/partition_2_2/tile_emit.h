#ifndef _JPEG_TILE_EMIT_H_INCLUDED
#define _JPEG_TILE_EMIT_H_INCLUDED

#include <stdint.h>
#include "structures.h"

/*
 * 2c raster sink output path (tile 2, partition 2).
 *
 * The pipeline preserves MCU order (FIFOs are FIFO, raster is single-core), so
 * MCUs arrive in raster order. We therefore use a simple APPEND COUNTER for the
 * scratch address - no index-derived slotting needed - and the existing ARM
 * scatter.c reads the result unchanged.
 *
 * Scratch layout from TILE_SCRATCH_START (1 KiB aligned):
 *   block 0       : geometry header
 *   block (1 + b) : tile-block b, holding `tiles_per_block` packed tiles
 * Header words: [0]=magic [1]=n_mcus [2]=MCU_sx [3]=MCU_sy
 *               [4]=x_size [5]=y_size [6]=n_comp [7]=tiles_per_block
 */
#define TILE_SCRATCH_START 0x15400000u
#define TILE_SCATTER_MAGIC 0x5CA77E12u

void tile_emit_init   (int x_size, int y_size, int MCU_sx, int MCU_sy, int n_comp);
void tile_emit_mcu    (const SubHeader2 *sh2, const ColorBuffer *cb);
void tile_emit_finish (void);
/* 0 = ok; 1 = framebuffer too large for this platform; 2 = scratch exhausted. */
int  tile_emit_failed (void);

#endif
