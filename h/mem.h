#ifndef __MEM_H__
#define __MEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "../h/env.h"

void writebyte_page(UCHAR page, USHORT offset, UCHAR value);
void writebyte_direct(unsigned offset, UCHAR value);
void writerom(USHORT address, UCHAR byte);
void writerom_indexed(unsigned romno, unsigned short address, unsigned char byte);

#ifdef __cplusplus
}
#endif

#endif
