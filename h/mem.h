#ifndef __MEM_H__
#define __MEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "../h/env.h"

void emul_writebyte_paged(UCHAR page, USHORT offset, UCHAR value);
void emul_writebyte_raw(unsigned offset, UCHAR value);
void emul_writerom(USHORT address, UCHAR byte);
void emul_writerom_no(unsigned romno, unsigned short address, unsigned char byte);

#ifdef __cplusplus
}
#endif

#endif
