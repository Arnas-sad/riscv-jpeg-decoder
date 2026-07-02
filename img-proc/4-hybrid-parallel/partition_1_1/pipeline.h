#ifndef _JPEG_PIPELINE_H_INCLUDED
#define _JPEG_PIPELINE_H_INCLUDED

#include "structures.h"

/*
 * 2d HYBRID pipeline tokens.
 *
 * Function-split AND data-split in one decoder:
 *
 *           f_freq_a  +--------------+  f_pix_a
 *   VLD+IQZZ ========> | IDCT-A (t1p1)| =========\
 *   (tile 0,  \        +--------------+           \  CC+raster
 *    source)   \ f_freq_b +--------------+ f_pix_b /  (tile 2, sink)
 *               ========>  | IDCT-B (t1p2)| =======/
 *
 * The source distributes MCUs ROUND-ROBIN (even index -> A, odd -> B); the
 * sink merges by strict alternation, which preserves raster order, so the
 * append-counter output path and the ARM scatter stay unchanged.
 *
 * Each token carries ONE MCU. `last` is the end-of-stream sentinel: the source
 * sends it to BOTH branch FIFOs; each worker forwards it; the sink stops
 * reading a branch when its sentinel arrives.
 *
 * Versus 2c (requirement: >=2 versions with different FIFO tokens and/or
 * depths): the hybrid uses DEPTH 4 (2c used 2) and a reduced token set
 * (freq/pix only - the coef and col hops are gone because VLD+IQZZ and
 * CC+raster are merged actors).
 */

/* VLD -> IQZZ+IDCT worker : raw (quantised, zigzagged) coefficient blocks. */
typedef struct {
  int        last;
  SubHeader1 sh1;
  SubHeader2 sh2;
  FValue     fv[10];
  int        nr_data_units;   /* real blocks/MCU (<=10); IQZZ/IDCT loop bound */
} tok_coef_t;

/* VLD+IQZZ -> IDCT worker : de-quantised, un-zigzagged frequency blocks. */
typedef struct {
  int        last;
  SubHeader1 sh1;
  SubHeader2 sh2;
  FBlock     fb[10];
  int        nr_data_units;   /* real blocks/MCU (<=10); IQZZ/IDCT loop bound */
} tok_freq_t;

/* IDCT worker -> CC+raster : pixel-space blocks. */
typedef struct {
  int        last;
  SubHeader1 sh1;
  SubHeader2 sh2;
  PBlock     pb[10];
} tok_pix_t;

#endif /* _JPEG_PIPELINE_H_INCLUDED */
