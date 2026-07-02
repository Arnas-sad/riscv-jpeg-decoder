/*
 * S5c output path - RISC-V side, with tile packing. DATA-PARALLEL (2b).
 * See tile_emit.h for the scratch layout and ownership scheme.
 * Compiled into the decode only on the board build (-DJPEG_DRAM_INPUT).
 */
#ifdef JPEG_DRAM_INPUT

#include <stdint.h>
#include <dma.h>
#include <platform.h>
#include <vep_shared_memory_regions.h>
#include "structures.h"
#include "actors.h"
#include "tile_emit.h"
#include "parallel.h"

/* Total 1 KiB blocks between the scratch base and the end of DRAM (incl. the
   header block). Tile-blocks may use 1..(SCRATCH_BLOCKS-1). */
#define SCRATCH_BLOCKS ((DRAM_END - TILE_SCRATCH_START) / 1024u)

static int      g_x, g_y, g_sx, g_sy, g_ncomp;
static int      g_tile_bytes;   /* MCU_sx*MCU_sy*4                          */
static int      g_per_block;    /* tiles packed into one 1 KiB block        */
static uint32_t g_total;        /* total MCUs in the image (mx*my)          */
static uint32_t g_cur_block;    /* absolute block index currently staged    */
static int      g_have;         /* tiles staged in tile_buf for g_cur_block */
static uint32_t g_owned;        /* MCUs this tile actually emitted          */
static int      g_fail;         /* 0 ok / 1 fb too big / 2 scratch full     */

void tile_emit_init(int x_size, int y_size, int MCU_sx, int MCU_sy, int n_comp)
{
  g_x = x_size; g_y = y_size; g_sx = MCU_sx; g_sy = MCU_sy; g_ncomp = n_comp;
  g_tile_bytes = MCU_sx * MCU_sy * 4;
  g_per_block  = (g_tile_bytes > 0) ? (int)(1024u / (uint32_t)g_tile_bytes) : 1;
  if (g_per_block < 1) g_per_block = 1;     /* MCU > 1 KiB: 1 per block   */

  /* Total MCUs = ceil(x/MCU_sx) * ceil(y/MCU_sy), in raster order. */
  {
    uint32_t mx = (MCU_sx > 0) ? (uint32_t)((x_size + MCU_sx - 1) / MCU_sx) : 0;
    uint32_t my = (MCU_sy > 0) ? (uint32_t)((y_size + MCU_sy - 1) / MCU_sy) : 0;
    g_total = mx * my;
  }

  g_cur_block = 0; g_have = 0; g_owned = 0; g_fail = 0;

  /* The ARM scatter writes x*y BGRA into the 64 MiB framebuffer region. */
  if ((uint64_t)x_size * (uint64_t)y_size * 4ull > (uint64_t)(DRAM_SIZE / 2))
    g_fail = 1;
}

int tile_emit_owns(uint32_t mcu_idx)
{
  if (g_per_block < 1) return 0;             /* not initialised yet        */
  return (int)((mcu_idx / (uint32_t)g_per_block) % (uint32_t)N_TILES)
         == TILE_ID;
}

uint32_t tile_emit_total(void) { return g_total; }

/* DMA the staged 1 KiB block out to its absolute scratch block (1 + block). */
static void flush_block(uint32_t block)
{
  if ((1u + block) >= SCRATCH_BLOCKS) { g_fail = 2; return; }
  volatile uint8_t *buf = TILE_REGION->tile_buf;
  uint32_t dst = TILE_SCRATCH_START + (1u + block) * 1024u;
  dma_send_to_dram(MY_DMA_QUEUE, buf, dst);
  while (dma_nr_pending_transactions(MY_DMA_QUEUE) > 0) asm("wfi");
}

void tile_emit_mcu(const SubHeader2 *sh2, const ColorBuffer *cb, uint32_t mcu_idx)
{
  if (g_fail) return;

  uint32_t block = mcu_idx / (uint32_t)g_per_block;
  int      slot  = (int)(mcu_idx % (uint32_t)g_per_block);

  /* Whole blocks are single-owner, so a tile fills one block's slots in order.
     If the block changes with data still staged (shouldn't happen with whole-
     block ownership), flush the old one first so nothing is lost. */
  if (g_have > 0 && block != g_cur_block) { flush_block(g_cur_block); g_have = 0; }
  g_cur_block = block;

  volatile uint8_t *buf  = TILE_REGION->tile_buf;
  volatile uint8_t *tile = buf + (uint32_t)slot * (uint32_t)g_tile_bytes;

  /* Edge MCUs fill only part of their slice; clear it so nothing stale leaks. */
  if (sh2->goodrows < g_sy || sh2->goodcolumns < g_sx)
    for (int k = 0; k < g_tile_bytes; k++) tile[k] = 0;

  /* Reuse raster.c: tile-local sub-header puts the MCU at this slot's origin
     with stride MCU_sx, writing BGRA into tile_buf at the slot offset. */
  SubHeader2 t = *sh2;
  t.fp.fb      = (volatile unsigned int *)tile;
  t.x_size     = g_sx;
  t.MCU_row    = 0;
  t.MCU_column = 1;
  raster(&t, cb);

  g_have++;
  g_owned++;
  if (slot == g_per_block - 1) { flush_block(block); g_have = 0; }
}

void tile_emit_finish(void)
{
  if (!g_fail && g_have > 0) flush_block(g_cur_block);   /* partial final block */

  /* Every cooperating tile writes the same geometry header (idempotent). The
     count is the TOTAL number of MCUs so scatter.c blits the whole image. */
  volatile uint32_t *hdr = (volatile uint32_t *) TILE_REGION->dma_scratch;
  hdr[0] = g_fail ? 0u : TILE_SCATTER_MAGIC;  /* 0 -> scatter refuses cleanly */
  hdr[1] = g_total;
  hdr[2] = (uint32_t)g_sx;
  hdr[3] = (uint32_t)g_sy;
  hdr[4] = (uint32_t)g_x;
  hdr[5] = (uint32_t)g_y;
  hdr[6] = (uint32_t)g_ncomp;
  hdr[7] = (uint32_t)g_per_block;

  dma_send_to_dram(MY_DMA_QUEUE, (volatile uint8_t *)hdr, TILE_SCRATCH_START);
  while (dma_nr_pending_transactions(MY_DMA_QUEUE) > 0) asm("wfi");
}

int tile_emit_failed(void) { return g_fail; }

#else
typedef int tile_emit_translation_unit_not_empty;
#endif /* JPEG_DRAM_INPUT */
