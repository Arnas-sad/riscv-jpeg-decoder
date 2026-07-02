/*
 * Post-decode framebuffer GATHER pass - RISC-V side. See fb_gather.h.
 *
 * 2d sink build: compiled UNCONDITIONALLY (no JPEG_DRAM_INPUT guard). The sink
 * always runs on the board and drives the DMA engine, exactly like its
 * tile_emit.c, which is also unguarded. (The 2b data-parallel copy of this file
 * is #ifdef JPEG_DRAM_INPUT-guarded because those partitions build with that
 * flag; the sink does not, so the guard is dropped here.)
 */
#include <stdint.h>
#include <dma.h>
#include <platform.h>
#include <xil_printf.h>
#include "tile_emit.h"   /* TILE_SCRATCH_START, TILE_SCATTER_MAGIC, header layout */
#include "fb_gather.h"

/* One 1 KiB DMA block holds 256 BGRA pixels (uint32 words). */
#define FB_WORDS_PER_BLOCK 256u

/* The gather is DMA-bound (many small transfers back-to-back), so spin on the
   queue rather than wfi: a wfi sleeps until the next TDM slot, which would cost
   a whole revolution per transfer. The transfer itself completes in ~300 cycles,
   so a busy spin within the slot lets us issue the next one immediately. */
#define DMA_WAIT(q) do { while (dma_nr_pending_transactions(q) > 0) {} } while (0)

int fb_gather(TileQueue_t dma_queue,
              volatile uint8_t *cache_buf, volatile uint8_t *out_buf)
{
  /* --- Pull the geometry header (scratch block 0) the decoder published. --- */
  dma_receive_from_dram(dma_queue, TILE_SCRATCH_START, cache_buf);
  DMA_WAIT(dma_queue);

  volatile uint32_t *hdr = (volatile uint32_t *)cache_buf;
  if (hdr[0] != TILE_SCATTER_MAGIC) {
    xil_printf("gather: bad magic 0x%08X - decoder did not finish\n", hdr[0]);
    return -1;
  }

  uint32_t sx      = hdr[2];   /* MCU_sx                              */
  uint32_t sy      = hdr[3];   /* MCU_sy                              */
  uint32_t xs      = hdr[4];   /* x_size                              */
  uint32_t ys      = hdr[5];   /* y_size                              */
  uint32_t per_blk = hdr[7];   /* tiles packed per 1 KiB scratch block */
  if (sx == 0 || sy == 0 || xs == 0 || ys == 0 || per_blk == 0) {
    xil_printf("gather: degenerate geometry sx=%u sy=%u xs=%u ys=%u per_blk=%u\n",
               sx, sy, xs, ys, per_blk);
    return -1;
  }

  uint32_t tile_words = sx * sy;                /* BGRA words per MCU tile        */
  uint32_t mx         = (xs + sx - 1) / sx;     /* MCU columns                    */
  uint32_t total      = xs * ys;                /* framebuffer pixels (BGRA words)*/
  uint32_t n_blocks   = (total + FB_WORDS_PER_BLOCK - 1) / FB_WORDS_PER_BLOCK;

  volatile uint32_t *out   = (volatile uint32_t *)out_buf;
  volatile uint32_t *cache = (volatile uint32_t *)cache_buf;

  /* Block 0 is the header, never a tile block; cached=0 means "no tile block
     loaded yet", so the first MCU forces a load (tile blocks are 1-based). */
  uint32_t cached = 0;

  for (uint32_t blk = 0; blk < n_blocks; blk++) {
    /* Coordinates are tracked incrementally to keep the inner loop divide-free:
       p->(x,y) and the within-MCU (i,j)/(mcol,mrow) only ever step by 1 or wrap.
       The two divides below run once per FB block, not once per pixel. */
    uint32_t p0   = blk * FB_WORDS_PER_BLOCK;      /* first pixel of this block   */
    uint32_t y    = p0 / xs;
    uint32_t x    = p0 - y * xs;
    uint32_t mrow = y / sy, i = y - mrow * sy;     /* MCU row,    row within MCU  */
    uint32_t mcol = x / sx, j = x - mcol * sx;     /* MCU column, col within MCU  */

    /* Per-MCU values (scratch block + tile base offset); recomputed only when
       the MCU index changes, which is every sx pixels - not every pixel. */
    uint32_t cur_idx  = 0xFFFFFFFFu;               /* force a recompute on pixel 0 */
    uint32_t tilebase = 0;

    for (uint32_t w = 0; w < FB_WORDS_PER_BLOCK; w++) {
      if (p0 + w >= total) { out[w] = 0; continue; }   /* pad past image end */

      uint32_t idx = mrow * mx + mcol;             /* global MCU index, raster order */
      if (idx != cur_idx) {
        uint32_t q    = idx / per_blk;             /* one divide per MCU */
        uint32_t sblk = 1u + q;                    /* scratch block (1-based) */
        if (sblk != cached) {                      /* reload only on block change */
          dma_receive_from_dram(dma_queue,
                                TILE_SCRATCH_START + sblk * 1024u, cache_buf);
          DMA_WAIT(dma_queue);
          cached = sblk;
        }
        tilebase = (idx - q * per_blk) * tile_words;  /* (idx % per_blk) * tile_words */
        cur_idx  = idx;
      }

      out[w] = cache[tilebase + i * sx + j];

      /* Advance to the next linear pixel, wrapping at the image edge. */
      if (++x == xs) {                             /* end of image row */
        x = 0; mcol = 0; j = 0;
        if (++i == sy) { i = 0; mrow++; }          /* crossed an MCU row */
      } else if (++j == sx) {                      /* crossed an MCU column */
        j = 0; mcol++;
      }
    }
    /* Write the fully-composed FB block exactly once. */
    dma_send_to_dram(dma_queue, out_buf, FRAME_BUFFER_START + blk * 1024u);
    DMA_WAIT(dma_queue);
  }
  return 0;
}

void fb_signal_done(TileQueue_t dma_queue, volatile uint8_t *buf)
{
  /* Read-modify-write the TOC block so only word 0 (the file count that
     dram.sh poll watches) is cleared; words 1..7 of the TOC are preserved. */
  dma_receive_from_dram(dma_queue, TOC_START, buf);
  DMA_WAIT(dma_queue);

  ((volatile uint32_t *)buf)[0] = 0;

  dma_send_to_dram(dma_queue, buf, TOC_START);
  DMA_WAIT(dma_queue);
}
