#ifndef _JPEG_ACTORS_H_INCLUDED
#define _JPEG_ACTORS_H_INCLUDED

#include "structures.h"

extern unsigned int get_num_pixels (VldHeader *header);
extern void init_header_vld (VldHeader *header, const unsigned int *data, unsigned int *fb);
extern int header_vld (VldHeader * header, VldHeader * updated_header, FValue mcu_after_vld[10], SubHeader1 * SH1, SubHeader2 * SH2);
extern void iqzz (const FValue * V, FBlock * B);
extern void idct (const FBlock * input, PBlock * output);
extern void cc (const SubHeader1 * SH1, const PBlock PB[], ColorBuffer * CB);
extern void raster (const SubHeader2 * SH2, const ColorBuffer * CB);

#endif
