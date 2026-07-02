#ifndef __VEP_PRIVATE_MEMORY_H_
#define __VEP_PRIVATE_MEMORY_H_

#include <stddef.h>
#include <stdint.h>
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
typedef volatile struct {
  uint32_t initialized;
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
typedef volatile struct {
  uint32_t initialized;
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
typedef volatile struct {
  uint32_t initialized;
} vep_tile2_partition1_shared_region_t;
#endif
#ifdef VEP_TILE2_PARTITION2_SHARED_REGION_REMOTE_START
typedef volatile struct {
  uint32_t initialized;
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
