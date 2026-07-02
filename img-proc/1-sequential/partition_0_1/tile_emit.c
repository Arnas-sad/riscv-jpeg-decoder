/*
 * S5c output path - RISC-V side, with tile packing. See tile_emit.h.
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

/* Total 1 KiB blocks between the scratch base and the end of DRAM (incl. the
   header block). Tile-blocks may use 1..(SCRATCH_BLOCKS-1). */
#define SCRATCH_BLOCKS ((DRAM_END - TILE_SCRATCH_START) / 1024u)

static int      g_x, g_y, g_sx, g_sy, g_ncomp;
static int      g_tile_bytes;   /* MCU_sx*MCU_sy*4                       */
static int      g_per_block;    /* tiles packed into one 1 KiB block     */
static int      g_slot;         /* tiles currently staged in tile_buf    */
static uint32_t g_block;        /* tile-blocks already flushed           */
static uint32_t g_idx;          /* MCUs accepted                         */
static int      g_fail;         /* 0 ok / 1 fb too big / 2 scratch full  */

void tile_emit_init(int x_size, int y_size, int MCU_sx, int MCU_sy, int n_comp)
{
  g_x = x_size; g_y = y_size; g_sx = MCU_sx; g_sy = MCU_sy; g_ncomp = n_comp;
  g_tile_bytes = MCU_sx * MCU_sy * 4;
  g_per_block  = (g_tile_bytes > 0) ? (int)(1024u / (uint32_t)g_tile_bytes) : 1;
  if (g_per_block < 1) g_per_block = 1;     /* MCU > 1 KiB: 1 per block   */
  g_slot = 0; g_block = 0; g_idx = 0; g_fail = 0;

  /* The ARM scatter writes x*y BGRA into the 64 MiB framebuffer region. */
  if ((uint64_t)x_size * (uint64_t)y_size * 4ull > (uint64_t)(DRAM_SIZE / 2))
    g_fail = 1;
}

/* DMA the staged 1 KiB block out to its tile-block slot (1 + g_block). */
static void flush_block(void)
{
  if ((1u + g_block) >= SCRATCH_BLOCKS) { g_fail = 2; return; }
  volatile uint8_t *buf = vep_tile0_partition1_shared_region->tile_buf;
  uint32_t dst = TILE_SCRATCH_START + (1u + g_block) * 1024u;
  dma_send_to_dram(DMA_QUEUE_0, buf, dst);
  while (dma_nr_pending_transactions(DMA_QUEUE_0) > 0) asm("wfi");
  g_block++;
}

void tile_emit_mcu(const SubHeader2 *sh2, const ColorBuffer *cb)
{
  g_idx++;                       /* count every MCU so the report is honest */
  if (g_fail) return;

  volatile uint8_t *buf  = vep_tile0_partition1_shared_region->tile_buf;
  volatile uint8_t *tile = buf + (uint32_t)g_slot * (uint32_t)g_tile_bytes;

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

  if (++g_slot >= g_per_block) { flush_block(); g_slot = 0; }
}

void tile_emit_finish(void)
{
  if (!g_fail && g_slot > 0) flush_block();   /* partial final block */

  volatile uint32_t *hdr =
      (volatile uint32_t *) vep_tile0_partition1_shared_region->dma_scratch;
  hdr[0] = g_fail ? 0u : TILE_SCATTER_MAGIC;  /* 0 -> scatter refuses cleanly */
  hdr[1] = g_idx;
  hdr[2] = (uint32_t)g_sx;
  hdr[3] = (uint32_t)g_sy;
  hdr[4] = (uint32_t)g_x;
  hdr[5] = (uint32_t)g_y;
  hdr[6] = (uint32_t)g_ncomp;
  hdr[7] = (uint32_t)g_per_block;

  dma_send_to_dram(DMA_QUEUE_0, (volatile uint8_t *)hdr, TILE_SCRATCH_START);
  while (dma_nr_pending_transactions(DMA_QUEUE_0) > 0) asm("wfi");
}

int tile_emit_failed(void) { return g_fail; }

#else
typedef int tile_emit_translation_unit_not_empty;
#endif /* JPEG_DRAM_INPUT */
