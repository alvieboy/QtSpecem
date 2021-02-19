#ifndef __ZEXPANSION_H__
#define __ZEXPANSION_H__

#include "../h/quirks.h"

#ifdef __cplusplus
extern "C" {
#endif

int register_expansion_port(USHORT port, USHORT mask,
                            UCHAR (*readfn)(void *,USHORT port),
                            void (*writefn)(void *,USHORT port, UCHAR val),
                            void *userdata
                           );

int readport_expansion(USHORT port, UCHAR *value);
int writeport_expansion(USHORT port, UCHAR value);
void writeport_ula(UCHAR value);

void set_enable_external_rom(int enabled);
int  get_enable_external_rom(void);

UCHAR readROM(USHORT addr);
UCHAR readRAM(USHORT addr);
void writeROM(USHORT addr, UCHAR value);

extern void rom_access_hook(USHORT,UCHAR);

#ifdef __cplusplus
}
#endif

#endif
