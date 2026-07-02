#ifndef __VEP_PRIVATE_MEMORY_H_
#define __VEP_PRIVATE_MEMORY_H_

#include <stddef.h>
#include <stdint.h>
#include <fifo.h>
#include <vep_memory_map.h>

// IMPORTANT: to use a private memory region you must:
// 1- declare the shared memory region in the vep-config.txt file
//    without this, the region will not be declared in the memory map
// 2- modify the typedef of the struct containing all data to be placed in the region
// 
// all fields are set to 0 when the VEP is loaded
// (it may therefore be useful to have an 'initialized' as first field in the struct)
//
// to debug, use the print_memory_map() function to print the VEP memory map
// it should be the same as the vep-config.txt from which it is generated
 

#ifdef VEP_MEMSHARED0_SHARED_REGION_REMOTE_START
/* 2c function-parallel pipeline FIFOs live in the vep-wide shared memory, which
   every tile/partition can address directly (libfifo buffers cannot live in
   DRAM - the RISC-V cannot dereference DRAM). Buffers are raw bytes sized by
   TOK_*_SIZE so this header need not see the token structs (defined in
   pipeline.h); each producer fifo_init's with sizeof(its token) and the
   partition code static-asserts sizeof(token) <= TOK_*_SIZE.

   Smoke test (step 1): a single coefficient FIFO, VLD (tile 0) -> consumer
   (tile 1). The remaining stage FIFOs are added when the pipeline is extended. */
/* 2d HYBRID: two pipeline branches (IDCT workers A and B on tile 1), DEPTH 4
   (vs 2c's depth 2 - the FIFO-dimensioning requirement differentiator).
   2 x (4 x 2816) + 2 x (4 x 832) + admin ~= 29.3 KiB < 32 KiB. */
#define PIPE_DEPTH     3
#define TOK_COEF_SIZE  3456u   /* >= sizeof(tok_coef_t): sh1+sh2 + 10*FValue      */
#define TOK_FREQ_SIZE  2816u   /* >= sizeof(tok_freq_t): sh1+sh2 + 10*FBlock      */
#define TOK_PIX_SIZE    832u   /* >= sizeof(tok_pix_t):  sh1+sh2 + 10*PBlock      */
typedef volatile struct {
  uint32_t initialized;
  fifo_t   f_freq_a;  uint8_t b_freq_a[PIPE_DEPTH * TOK_COEF_SIZE]; /* src -> IDCT-A */
  fifo_t   f_freq_b;  uint8_t b_freq_b[PIPE_DEPTH * TOK_COEF_SIZE]; /* src -> IDCT-B */
  fifo_t   f_pix_a;   uint8_t b_pix_a [PIPE_DEPTH * TOK_PIX_SIZE];  /* IDCT-A -> sink */
  fifo_t   f_pix_b;   uint8_t b_pix_b [PIPE_DEPTH * TOK_PIX_SIZE];  /* IDCT-B -> sink */
} vep_memshared0_shared_region_t;
#endif

#ifdef VEP_TILE0_PARTITION1_SHARED_REGION_REMOTE_START
/* Three 1 KiB-aligned DMA staging buffers. The shared-region base is 1 KiB
   aligned, and each field is exactly 1024 bytes, so every buffer is a valid
   1 KiB-aligned DMA endpoint:
     dma_scratch - TOC read and ad-hoc transfers
     input_block - one cached 1 KiB block of the JPEG (windowed reader)
     tile_buf    - one rasterised MCU, DMAed out to the scratch area */
typedef volatile struct {
  uint8_t dma_scratch[1024];
  uint8_t input_block[1024];
  uint8_t tile_buf[1024];
} vep_tile0_partition1_shared_region_t;
#endif
#ifdef VEP_TILE0_PARTITION2_SHARED_REGION_REMOTE_START
typedef volatile struct {
  uint32_t initialized;
} vep_tile0_partition2_shared_region_t;
#endif
#ifdef VEP_TILE0_PARTITION3_SHARED_REGION_REMOTE_START
typedef volatile struct {
  uint32_t initialized;
} vep_tile0_partition3_shared_region_t;
#endif
#ifdef VEP_TILE0_PARTITION4_SHARED_REGION_REMOTE_START
typedef volatile struct {
  uint32_t initialized;
} vep_tile0_partition4_shared_region_t;
#endif
#ifdef VEP_TILE1_PARTITION1_SHARED_REGION_REMOTE_START
/* Data-parallel: tile 1's private 3 KiB of DMA staging buffers, same layout as
   tile 0 (dma_scratch / input_block / tile_buf), each a 1 KiB DMA endpoint. */
typedef volatile struct {
  uint8_t dma_scratch[1024];
  uint8_t input_block[1024];
  uint8_t tile_buf[1024];
} vep_tile1_partition1_shared_region_t;
#endif
#ifdef VEP_TILE1_PARTITION2_SHARED_REGION_REMOTE_START
typedef volatile struct {
  uint32_t initialized;
} vep_tile1_partition2_shared_region_t;
#endif
#ifdef VEP_TILE1_PARTITION3_SHARED_REGION_REMOTE_START
typedef volatile struct {
  uint32_t initialized;
} vep_tile1_partition3_shared_region_t;
#endif
#ifdef VEP_TILE1_PARTITION4_SHARED_REGION_REMOTE_START
typedef volatile struct {
  uint32_t initialized;
} vep_tile1_partition4_shared_region_t;
#endif
#ifdef VEP_TILE2_PARTITION1_SHARED_REGION_REMOTE_START
/* Data-parallel: tile 2's private 3 KiB of DMA staging buffers, same layout as
   tile 0 (dma_scratch / input_block / tile_buf), each a 1 KiB DMA endpoint. */
typedef volatile struct {
  uint8_t dma_scratch[1024];
  uint8_t input_block[1024];
  uint8_t tile_buf[1024];
} vep_tile2_partition1_shared_region_t;
#endif
#ifdef VEP_TILE2_PARTITION2_SHARED_REGION_REMOTE_START
/* 2c/2d raster sink (tile 2 partition 2) DMA staging: tile_buf is rasterised
   into and DMAed to the DRAM scratch area; dma_scratch carries the geometry
   header (and the TOC for the gather's done-signal); input_block is the gather
   pass's scratch-block read cache. Each field is a 1 KiB-aligned DMA endpoint,
   filling the 3 KiB region exactly. */
typedef volatile struct {
  uint8_t dma_scratch[1024];
  uint8_t tile_buf[1024];
  uint8_t input_block[1024];
} vep_tile2_partition2_shared_region_t;
#endif
#ifdef VEP_TILE2_PARTITION3_SHARED_REGION_REMOTE_START
typedef volatile struct {
  uint32_t initialized;
} vep_tile2_partition3_shared_region_t;
#endif
#ifdef VEP_TILE2_PARTITION4_SHARED_REGION_REMOTE_START
typedef volatile struct {
  uint32_t initialized;
} vep_tile2_partition4_shared_region_t;
#endif



/***** DO NOT MODIFY THE CODE BELOW *****/

extern void print_vep_memory_map(void);

#ifdef VEP_MEMSHARED0_SHARED_REGION_REMOTE_START
extern vep_memshared0_shared_region_t volatile * const vep_memshared0_shared_region;
#endif

#ifdef VEP_TILE0_PARTITION1_SHARED_REGION_REMOTE_START
extern vep_tile0_partition1_shared_region_t volatile * const vep_tile0_partition1_shared_region;
#endif
#ifdef VEP_TILE0_PARTITION2_SHARED_REGION_REMOTE_START
extern vep_tile0_partition2_shared_region_t volatile * const vep_tile0_partition2_shared_region;
#endif
#ifdef VEP_TILE0_PARTITION3_SHARED_REGION_REMOTE_START
extern vep_tile0_partition3_shared_region_t volatile * const vep_tile0_partition3_shared_region;
#endif
#ifdef VEP_TILE0_PARTITION4_SHARED_REGION_REMOTE_START
extern vep_tile0_partition4_shared_region_t volatile * const vep_tile0_partition4_shared_region;
#endif
#ifdef VEP_TILE1_PARTITION1_SHARED_REGION_REMOTE_START
extern vep_tile1_partition1_shared_region_t volatile * const vep_tile1_partition1_shared_region;
#endif
#ifdef VEP_TILE1_PARTITION2_SHARED_REGION_REMOTE_START
extern vep_tile1_partition2_shared_region_t volatile * const vep_tile1_partition2_shared_region;
#endif
#ifdef VEP_TILE1_PARTITION3_SHARED_REGION_REMOTE_START
extern vep_tile1_partition3_shared_region_t volatile * const vep_tile1_partition3_shared_region;
#endif
#ifdef VEP_TILE1_PARTITION4_SHARED_REGION_REMOTE_START
extern vep_tile1_partition4_shared_region_t volatile * const vep_tile1_partition4_shared_region;
#endif
#ifdef VEP_TILE2_PARTITION1_SHARED_REGION_REMOTE_START
extern vep_tile2_partition1_shared_region_t volatile * const vep_tile2_partition1_shared_region;
#endif
#ifdef VEP_TILE2_PARTITION2_SHARED_REGION_REMOTE_START
extern vep_tile2_partition2_shared_region_t volatile * const vep_tile2_partition2_shared_region;
#endif
#ifdef VEP_TILE2_PARTITION3_SHARED_REGION_REMOTE_START
extern vep_tile2_partition3_shared_region_t volatile * const vep_tile2_partition3_shared_region;
#endif
#ifdef VEP_TILE2_PARTITION4_SHARED_REGION_REMOTE_START
extern vep_tile2_partition4_shared_region_t volatile * const vep_tile2_partition4_shared_region;
#endif

#endif
