#ifndef _JPEG_PIPELINE_H_INCLUDED
#define _JPEG_PIPELINE_H_INCLUDED

#include "structures.h"

/*
 * 2c function-parallel pipeline tokens.
 *
 * Each token carries ONE MCU between stages (macroblock granularity). The
 * sub-headers travel with the data so downstream stages have the geometry /
 * colour config they need. `last` is an end-of-stream sentinel (no payload):
 * the source emits it at EOI, each stage forwards it and then stops.
 *
 * Stage graph:   VLD --coef--> IQZZ --freq--> IDCT --pix--> CC --col--> raster
 *
 * The token structs are defined here; the shared-memory region (vep_shared_-
 * memory_regions.h) only knows their byte SIZE (TOK_*_SIZE) so it stays
 * independent of these definitions. Keep TOK_*_SIZE >= sizeof(token) - the
 * partition code static-asserts this.
 */

/* VLD -> IQZZ : raw coefficients (dequant input) for one MCU. */
typedef struct {
  int        last;
  SubHeader1 sh1;
  SubHeader2 sh2;
  FValue     fv[10];
  int        nr_data_units;   /* real blocks/MCU (<=10); IQZZ loop bound (S-01) */
} tok_coef_t;

/* IQZZ -> IDCT : de-quantised, un-zigzagged frequency blocks. */
typedef struct {
  int        last;
  SubHeader1 sh1;
  SubHeader2 sh2;
  FBlock     fb[10];
  int        nr_data_units;   /* real blocks/MCU (<=10); IDCT loop bound (S-01) */
} tok_freq_t;

/* IDCT -> CC : pixel-space blocks. */
typedef struct {
  int        last;
  SubHeader1 sh1;
  SubHeader2 sh2;
  PBlock     pb[10];
  int        nr_data_units;   /* real blocks/MCU (<=10); CC copy bound (S-02) */
} tok_pix_t;

/* CC -> raster : the assembled BGR(A) colour buffer for one MCU. */
typedef struct {
  int         last;
  SubHeader2  sh2;
  ColorBuffer cb;
} tok_col_t;

#endif /* _JPEG_PIPELINE_H_INCLUDED */
