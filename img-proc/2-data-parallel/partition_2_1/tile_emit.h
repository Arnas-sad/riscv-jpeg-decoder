#ifndef _JPEG_TILE_EMIT_H_INCLUDED
#define _JPEG_TILE_EMIT_H_INCLUDED

#include <stdint.h>
#include "structures.h"

/*
 * S5c output path - RISC-V side, DATA-PARALLEL (2b).
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
 *
 * DATA-PARALLEL OWNERSHIP. The output address is derived from the GLOBAL MCU
 * index, NOT from arrival order, so the cooperating tiles' out-of-order DMA
 * writes self-sort by address. Whole 1 KiB blocks are assigned round-robin to
 * tiles (block b -> tile b % N_TILES), so two tiles never co-write one block
 * and no locks are needed. Each tile DMAs into its OWN scratch blocks via its
 * OWN queue (see parallel.h). Every tile runs the full VLD to keep the
 * bitstream/DC-predictor state, but only emits the MCUs it owns.
 *
 * The geometry header carries the TOTAL MCU count (mx*my); every tile writes
 * the identical header, so scatter.c sees the whole image regardless of which
 * tile finished last.
 */
#define TILE_SCRATCH_START 0x15400000u
#define TILE_SCATTER_MAGIC 0x5CA77E12u

void     tile_emit_init   (int x_size, int y_size, int MCU_sx, int MCU_sy, int n_comp);
/* 1 if this tile owns (must decode/emit) the MCU at this global index. */
int      tile_emit_owns   (uint32_t mcu_idx);
/* Emit an owned MCU at its global index. Caller must have checked tile_emit_owns. */
void     tile_emit_mcu    (const SubHeader2 *sh2, const ColorBuffer *cb, uint32_t mcu_idx);
void     tile_emit_finish (void);
/* 0 = ok; 1 = framebuffer too large for this platform; 2 = scratch exhausted. */
int      tile_emit_failed (void);
/* Total MCUs in the image (mx*my); valid after tile_emit_init. */
uint32_t tile_emit_total  (void);

#endif
