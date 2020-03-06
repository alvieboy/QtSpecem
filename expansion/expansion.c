#include "expansion.h"
#include <stddef.h>
#include <stdio.h>

#define MAX_EXPANDERS 8

struct ioexpand {
    UCHAR port;
    UCHAR mask;
    UCHAR (*readfn)(void*,UCHAR port);
    void (*writefn)(void*,UCHAR port, UCHAR val);
    void *userdata;
};

struct ioexpand expanders[MAX_EXPANDERS];
static unsigned num_expanders = 0;

int readport_expansion(UCHAR port, UCHAR *value)
{
    unsigned i;
    int r = -1;
    printf("Check IO %d\n", port);
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

int writeport_expansion(UCHAR port, UCHAR value)
{
    unsigned i;
    int r = -1;

    for (i=0;i<num_expanders;i++) {
        if ( (port & expanders[i].mask) ==
            expanders[i].port ) {
            expanders[i].writefn(expanders[i].userdata,port, value);
            r = 0;
            break;
        }
    }
    return r;
}

int register_expansion_port(UCHAR port, UCHAR mask,
                            UCHAR (*readfn)(void*,UCHAR port),
                            void (*writefn)(void*,UCHAR port, UCHAR val),
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
static UCHAR *current_rom = NULL;

void set_current_rom(UCHAR *address)
{
    current_rom = address;
}

void __attribute__((weak)) rom_access_hook(USHORT address __attribute__((unused)),
                                           UCHAR val __attribute__((unused)))
{
}

UCHAR readROM(USHORT addr)
{
    UCHAR val;
    if (!current_rom)
        val =  mem[addr];
    else
        val = current_rom[addr];
    rom_access_hook(addr,val);
    return val;
}

UCHAR readRAM(USHORT addr)
{
    if (addr<0x4000)
        return readROM(addr);
    return mem[addr];
}
