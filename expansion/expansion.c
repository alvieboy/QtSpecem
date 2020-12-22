#include "expansion.h"
#include <stddef.h>
#include <stdio.h>

#define MAX_EXPANDERS 8

struct ioexpand {
    USHORT port;
    USHORT mask;
    UCHAR (*readfn)(void*,USHORT port);
    void (*writefn)(void*,USHORT port, UCHAR val);
    void *userdata;
};

struct ioexpand expanders[MAX_EXPANDERS];
static unsigned num_expanders = 0;

int readport_expansion(USHORT port, UCHAR *value)
{
    unsigned i;
    int r = -1;
//    printf("Check IO %d\n", port);
    for (i=0;i<num_expanders;i++) {
        if ( (port & expanders[i].mask) ==
            expanders[i].port ) {
            *value = expanders[i].readfn(expanders[i].userdata,port);
            r = 0;
            break;
        }
    }
    return r;
}

int writeport_expansion(USHORT port, UCHAR value)
{
    unsigned i;
    int r = -1;

    for (i=0;i<num_expanders;i++) {
        //printf("Check %02x mask %02x port %02x\n", port, expanders[i].mask, expanders[i].port);
        if ( (port & expanders[i].mask) ==
            expanders[i].port ) {
          //  printf("Match\n");
            expanders[i].writefn(expanders[i].userdata,port, value);
            r = 0;
            break;
        }
    }
    return r;
}

int register_expansion_port(USHORT port, USHORT mask,
                            UCHAR (*readfn)(void*,USHORT port),
                            void (*writefn)(void*,USHORT port, UCHAR val),
                            void *userdata)
{
    if (num_expanders+1 == MAX_EXPANDERS)
        return -1;
    expanders[num_expanders].port = port & mask;
    expanders[num_expanders].mask = mask;
    expanders[num_expanders].readfn = readfn;
    expanders[num_expanders].writefn = writefn;
    expanders[num_expanders].userdata = userdata;
    num_expanders++;
    return 0;
}


extern UCHAR *mem;
static int external_rom_enabled = 0;

void set_enable_external_rom(int enabled)
{
    printf("Set external ROM enabled: %d\n", enabled);
    external_rom_enabled = enabled;
}

int get_enable_external_rom()
{
    return external_rom_enabled;
}

extern UCHAR external_rom_read(USHORT address);
extern void external_rom_write(USHORT address, UCHAR value);

extern int rom_is_hooked(USHORT address);

extern void external_rom_write(USHORT address, UCHAR value);

void __attribute__((weak)) rom_access_hook(USHORT address __attribute__((unused)),
                                           UCHAR val __attribute__((unused)))
{
}

UCHAR readROM(USHORT addr)
{
    UCHAR val;
    if (!external_rom_enabled) {
        // If we have hooks, then we need to load them
        if (rom_is_hooked(addr)) {
            val = external_rom_read(addr);
            //printf("HOOKED %04x %02x\n", addr, val);
        } else {
            val =  mem[addr];
        }
    }
    else {
        val = external_rom_read(addr);
    }
    rom_access_hook(addr,val);
    return val;
}

UCHAR readRAM(USHORT addr)
{
    if (addr<0x4000)
        return readROM(addr);
    return mem[addr];
}

void writeROM(USHORT addr, UCHAR value)
{
    if (!external_rom_enabled)
        return;
    external_rom_write(addr,value);
}

extern unsigned short get_pc();

void writeRAM(USHORT addr, UCHAR value)
{
    mem[addr] = value;
}
