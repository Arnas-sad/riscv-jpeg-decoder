/*
 * Windowed DRAM input reader (S3) - RISC-V JPEG sequential decoder.
 *
 * Provides the FGETC() the actors expect, but instead of indexing
 * fp->data[...] (which would be a direct DRAM dereference and fault on the
 * RISC-V), it DMAs the 1 KiB block that contains the requested byte into the
 * shared-region `input_block` buffer and returns the byte from there.
 *
 * Only one block is cached. The VLD reads vld_count forward, so it crosses a
 * block boundary roughly every 1024 bytes; each crossing costs one DMA. A
 * backward FSEEK simply causes a cache miss and a re-fetch, so it stays
 * correct even though it is not the common case.
 *
 * Compiled into the decode only when -DJPEG_DRAM_INPUT is set (see Makefile).
 * Under that flag the reference FGETC in 5lib0.c is #ifndef'd out, so exactly
 * one definition of FGETC exists in the RISC-V link.
 */
#ifdef JPEG_DRAM_INPUT

#include <stdint.h>
#include <dma.h>
#include <platform.h>
#include <vep_shared_memory_regions.h>
#include "structures.h"
#include "5lib0.h"
#include "dram_input.h"

#define DRAM_BLOCK_SIZE  1024u
#define DRAM_BLOCK_MASK  (~(DRAM_BLOCK_SIZE - 1u))

/* Absolute DRAM byte address of JPEG byte 0 (from TOC word [2]). */
static uint32_t g_jpeg_base;
/* DRAM address of the 1 KiB block currently held in input_block. */
static uint32_t g_cached_block;
/* 0 until the first block has been fetched (g_cached_block not yet valid). */
static int      g_have_block;

void dram_input_init (uint32_t jpeg_dram_addr)
{
  g_jpeg_base    = jpeg_dram_addr;
  g_cached_block = 0;
  g_have_block   = 0;
}

/* DMA one 1 KiB block from DRAM into the shared input_block buffer. block_addr
   must be 1 KiB aligned; g_jpeg_base is 1 KiB aligned and the mask keeps it so. */
static void load_block (uint32_t block_addr)
{
  volatile uint8_t *dst = vep_tile0_partition1_shared_region->input_block;
  dma_receive_from_dram (DMA_QUEUE_0, block_addr, dst);
  while (dma_nr_pending_transactions (DMA_QUEUE_0) > 0) asm ("wfi");
  g_cached_block = block_addr;
  g_have_block   = 1;
}

unsigned int FGETC (JPGFile *fp)
{
  uint32_t idx = (uint32_t) fp->vld_count;      /* byte offset into the JPEG  */
  fp->vld_count++;

  uint32_t abs_addr  = g_jpeg_base + idx;        /* absolute DRAM byte address */
  uint32_t block_addr = abs_addr & DRAM_BLOCK_MASK;

  if (!g_have_block || block_addr != g_cached_block)
    load_block (block_addr);

  volatile uint8_t *buf = vep_tile0_partition1_shared_region->input_block;
  return (unsigned int) buf[abs_addr - block_addr];
}

#else
/* Keep this a non-empty translation unit when the flag is absent. */
typedef int dram_input_translation_unit_not_empty;
#endif /* JPEG_DRAM_INPUT */
