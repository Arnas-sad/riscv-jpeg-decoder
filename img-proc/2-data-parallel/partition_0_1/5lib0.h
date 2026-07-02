#ifndef __5KK03_H__
#define __5KK03_H__
#include <stdio.h> /* only for EOF */
#include "structures.h"

unsigned int FGETC (JPGFile *fp);
int FSEEK (JPGFile *fp, int offset, int start);
unsigned int FTELL (JPGFile *fp);

#endif
