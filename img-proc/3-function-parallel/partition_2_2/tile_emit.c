/*
 * 2c raster sink output path - append-counter packing + DMA to DRAM scratch.
 * Single-core, in-order (see tile_emit.h). Uses tile 2 / partition 2's private
 * DMA region and DMA queue 2.
 */
#include <stdint.h>
#include <dma.h>
#include <platform.h>
#include <vep_shared_memory_regions.h>
#include "structures.h"
#include "actors.h"
#include "tile_emit.h"

#define SCRATCH_BLOCKS  ((DRAM_END - TILE_SCRATCH_START) / 1024u)
#define RASTER_REGION   vep_tile2_partition2_shared_region
#define RASTER_QUEUE    DMA_QUEUE_2

static int      g_x, g_y, g_sx, g_sy, g_ncomp;
static int      g_tile_bytes;   /* MCU_sx*MCU_sy*4                    */
static int      g_per_block;    /* tiles packed into one 1 KiB block  */
static int      g_slot;         /* tiles staged in tile_buf           */
static uint32_t g_block;        /* tile-blocks already flushed        */
static uint32_t g_idx;          /* MCUs accepted (= total, in order)  */
static int      g_fail;         /* 0 ok / 1 fb too big / 2 scratch    */

void tile_emit_init(int x_size, int y_size, int MCU_sx, int MCU_sy, int n_comp)
{
  g_x = x_size; g_y = y_size; g_sx = MCU_sx; g_sy = MCU_sy; g_ncomp = n_comp;
  g_tile_bytes = MCU_sx * MCU_sy * 4;
  g_per_block  = (g_tile_bytes > 0) ? (int)(1024u / (uint32_t)g_tile_bytes) : 1;
  if (g_per_block < 1) g_per_block = 1;
  g_slot = 0; g_block = 0; g_idx = 0; g_fail = 0;

  if ((uint64_t)x_size * (uint64_t)y_size * 4ull > (uint64_t)(DRAM_SIZE / 2))
    g_fail = 1;
}

static void flush_block(void)
{
  if ((1u + g_block) >= SCRATCH_BLOCKS) { g_fail = 2; return; }
  volatile uint8_t *buf = RASTER_REGION->tile_buf;
  uint32_t dst = TILE_SCRATCH_START + (1u + g_block) * 1024u;
  dma_send_to_dram(RASTER_QUEUE, buf, dst);
  while (dma_nr_pending_transactions(RASTER_QUEUE) > 0) asm("wfi");
  g_block++;
}

void tile_emit_mcu(const SubHeader2 *sh2, const ColorBuffer *cb)
{
  g_idx++;
  if (g_fail) return;

  volatile uint8_t *buf  = RASTER_REGION->tile_buf;
  volatile uint8_t *tile = buf + (uint32_t)g_slot * (uint32_t)g_tile_bytes;

  if (sh2->goodrows < g_sy || sh2->goodcolumns < g_sx)
    for (int k = 0; k < g_tile_bytes; k++) tile[k] = 0;

  /* Reuse raster.c tile-local: write BGRA into tile_buf at this slot. */
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
  if (!g_fail && g_slot > 0) flush_block();

  volatile uint32_t *hdr = (volatile uint32_t *) RASTER_REGION->dma_scratch;
  hdr[0] = g_fail ? 0u : TILE_SCATTER_MAGIC;
  hdr[1] = g_idx;                 /* total MCUs, in raster order */
  hdr[2] = (uint32_t)g_sx;
  hdr[3] = (uint32_t)g_sy;
  hdr[4] = (uint32_t)g_x;
  hdr[5] = (uint32_t)g_y;
  hdr[6] = (uint32_t)g_ncomp;
  hdr[7] = (uint32_t)g_per_block;

  dma_send_to_dram(RASTER_QUEUE, (volatile uint8_t *)hdr, TILE_SCRATCH_START);
  while (dma_nr_pending_transactions(RASTER_QUEUE) > 0) asm("wfi");
}

int tile_emit_failed(void) { return g_fail; }
