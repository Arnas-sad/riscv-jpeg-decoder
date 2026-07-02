#ifndef _JPEG_DRAM_INPUT_H_INCLUDED
#define _JPEG_DRAM_INPUT_H_INCLUDED

#include <stdint.h>

/*
 * Windowed DRAM input for the RISC-V JPEG decoder (S3).
 *
 * The RISC-V tile cannot dereference DRAM directly (the MMU kills the
 * partition), so the reference FGETC's `fp->data[...]` access is replaced (in
 * dram_input.c, under -DJPEG_DRAM_INPUT) by a reader that DMAs the JPEG stream
 * one 1 KiB block at a time into the shared-region `input_block` buffer.
 *
 * Because the VLD walks `vld_count` essentially monotonically forward, caching
 * a single block is enough: a new block is fetched only when the requested byte
 * falls outside the currently cached block.
 *
 * Call dram_input_init() once, after reading the TOC, with the JPEG's absolute
 * DRAM byte address (TOC word [2], e.g. 0x14000400). It must be 1 KiB aligned,
 * which the loader guarantees for files placed after the TOC block.
 */
void dram_input_init (uint32_t jpeg_dram_addr);

#endif
