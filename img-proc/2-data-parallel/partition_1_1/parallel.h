#ifndef _JPEG_PARALLEL_H_INCLUDED
#define _JPEG_PARALLEL_H_INCLUDED

/*
 * Data-parallel (2b) per-tile configuration.
 *
 * The decoder source is identical on every cooperating tile; this header is the
 * only thing that specialises a build to its tile. TILE_ID is #defined by the
 * build (parsed from the partition_<TILE>_<PARTITION> directory name, see
 * make_common.mk), so each of partition_0_1 / partition_1_1 / partition_2_1
 * compiles the same code into a tile-specific binary.
 *
 * Each tile is given:
 *   - its own DMA queue        (queues are independent in hardware -> no locks),
 *   - its own shared region    (private 3 KiB of DMA staging buffers),
 *   - a slice of the MCUs       (chosen by ownership, see tile_emit.c).
 *
 * N_TILES is how many tiles share the decode. With N_TILES == 1 the ownership
 * test degenerates to "this tile owns everything", i.e. the sequential decoder.
 */

#include <dma.h>
#include <vep_shared_memory_regions.h>

#ifndef N_TILES
#define N_TILES 3
#endif

#ifndef TILE_ID
#error "TILE_ID must be defined by the build (partition_<TILE>_<PARTITION> dir)"
#endif

#if   TILE_ID == 0
#define TILE_REGION   vep_tile0_partition1_shared_region
#define MY_DMA_QUEUE  DMA_QUEUE_0
#elif TILE_ID == 1
#define TILE_REGION   vep_tile1_partition1_shared_region
#define MY_DMA_QUEUE  DMA_QUEUE_1
#elif TILE_ID == 2
#define TILE_REGION   vep_tile2_partition1_shared_region
#define MY_DMA_QUEUE  DMA_QUEUE_2
#else
#error "TILE_ID must be 0, 1 or 2 for the data-parallel JPEG decoder"
#endif

#endif /* _JPEG_PARALLEL_H_INCLUDED */
