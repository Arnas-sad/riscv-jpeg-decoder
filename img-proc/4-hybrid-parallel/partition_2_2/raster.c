#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "structures.h"

void raster (const SubHeader2 * SH2, const ColorBuffer * CB)
{
  const int     goodrows    = SH2->goodrows;
  const int     goodcolumns = SH2->goodcolumns;
  const int     MCU_sx      = SH2->MCU_sx;
  const int     n_comp      = SH2->n_comp;
  const int     x_size      = SH2->x_size;

  volatile BGRA *mem = (BGRA *) SH2->fp.fb;
  for (int i = 0; i < goodrows; i++) {
    const int row_offset = (i + SH2->MCU_row * SH2->MCU_sy) * x_size;
    for (int j = 0; j < goodcolumns; j++) {
      int col_offset = j + (SH2->MCU_column - 1) * SH2->MCU_sx;
      if (n_comp == 3) {
        mem[row_offset + col_offset].pixel.r = CB->data[n_comp * i * MCU_sx + 2 + j * n_comp];
        mem[row_offset + col_offset].pixel.g = CB->data[n_comp * i * MCU_sx + 1 + j * n_comp];
        mem[row_offset + col_offset].pixel.b = CB->data[n_comp * i * MCU_sx + 0 + j * n_comp];
        mem[row_offset + col_offset].pixel.a = 0;
      }
      else if (n_comp == 1) {
        mem[row_offset + col_offset].pixel.r = CB->data[n_comp * i * MCU_sx + 0 + j * n_comp];
        mem[row_offset + col_offset].pixel.g = CB->data[n_comp * i * MCU_sx + 0 + j * n_comp];
        mem[row_offset + col_offset].pixel.b = CB->data[n_comp * i * MCU_sx + 0 + j * n_comp];
        mem[row_offset + col_offset].pixel.a = 0;
      }
    }
  }
}
