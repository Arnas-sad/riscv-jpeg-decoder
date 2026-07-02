#ifndef _JPEG_TILE_EMIT_H_INCLUDED
#define _JPEG_TILE_EMIT_H_INCLUDED

#include <stdint.h>
#include "structures.h"

/*
 * S5c output path - RISC-V side.
 *
 * Each decoded MCU is rasterised tile-local and staged in a DRAM scratch area;
 * an ARM helper (scatter.c) blits the tiles into the framebuffer afterwards.
 *
 * Tiles are PACKED: a 1 KiB DMA block holds (1024 / tile_bytes) tiles, so 8x8
 * MCUs (256 B) pack 4 per block and 16x16 MCUs (1024 B) sit 1 per block. This
 * keeps even multi-megapixel images inside the ~44 MiB scratch window.
 *
 * Scratch layout from TILE_SCRATCH_START (1 KiB aligned):
 *   block 0           : geometry header
 *   block (1 + b)     : tile-block b, holding `tiles_per_block` packed tiles
 * MCU idx lives in tile-block idx/tiles_per_block, at slot idx%tiles_per_block
 * (byte offset slot*tile_bytes within the block).
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
