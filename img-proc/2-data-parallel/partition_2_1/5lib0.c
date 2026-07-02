#include "structures.h"
#include "5lib0.h"

/* Direct-array reader: used by the ARM reference build, where fp->data points
   at the JPEG bytes in directly-addressable memory. On the RISC-V build
   (-DJPEG_DRAM_INPUT) this is replaced by the windowed DMA reader in
   dram_input.c, because the RISC-V cannot dereference DRAM directly. FSEEK and
   FTELL below operate purely on vld_count, so they are shared by both builds. */
#ifndef JPEG_DRAM_INPUT
unsigned int FGETC (JPGFile *fp)
{
  unsigned int c = ((fp->data[fp->vld_count / 4] << (8 * (3 - (fp->vld_count % 4)))) >> 24) & 0x00ff;
  fp->vld_count++;
  return c;
}
#endif

int FSEEK (JPGFile *fp, int offset, int start)
{
  fp->vld_count += offset + (start - start);    /* Just to use start... */
  return 0;
}

unsigned int FTELL (JPGFile *fp)
{
  return fp->vld_count;
}
