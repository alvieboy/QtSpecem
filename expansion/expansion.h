#ifndef __ZEXPANSION_H__
#define __ZEXPANSION_H__

#include "../h/quirks.h"

#ifdef __cplusplus
extern "C" {
#endif

int register_expansion_port(UCHAR port, UCHAR mask,
                            UCHAR (*readfn)(void *,UCHAR port),
                            void (*writefn)(void *,UCHAR port, UCHAR val),
                            void *userdata
                           );

int readport_expansion(UCHAR port, UCHAR *value);
int writeport_expansion(UCHAR port, UCHAR value);

void set_current_rom(const UCHAR *address);

UCHAR readROM(USHORT addr);
UCHAR readRAM(USHORT addr);

extern void rom_access_hook(USHORT,UCHAR);

#ifdef __cplusplus
}
#endif

#endif
