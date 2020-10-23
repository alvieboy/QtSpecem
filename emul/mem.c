/* Mem.c: Z80 memory - basic support routines.
 *
 * Copyright 1991-2019 Rui Fernando Ferreira Ribeiro.
 *
 */

//#include "../h/quirks.h"
#include "../h/env.h"
#include <stdio.h>
    
//static UCHAR inited = 0;


#if !defined(NDEBUG)
extern UCHAR running;
#endif

extern UCHAR *memory;
extern UCHAR *rommemory;


    /*
     48K             128K
     -               page 0
     8000-bfff       page 1     // 10b
     c000-ffff       page 2     // 11b
     -               page 3     
     -               page 4
     4000-7fff       page 5     // 01b
     -               page 6
     -               page 7
     */

unsigned char readrom(unsigned short address);

extern void video_writebyte(unsigned short adress, unsigned char byte);

unsigned char mempage128     = 0; // This is not in order!
unsigned mempage_offset = 0; // This is not in order!
unsigned rom_offset = 0;

/* This table maps between the 0x7FFD port setting
 and the actual map (offset) we use for the RAM */

static unsigned char rampage_map[] = {
    0, // 0  Page 0, map into 0x00000-0x03FFF
    3, // 1  Page 1, map into 0x0C000-0x0FFFF
    2, // 2  Page 2, map into 0x08000-0x0BFFF
    4, // 3  Page 3, map into 0x10000-0x13FFF
    5, // 4  Page 4, map into 0x14000-0x17FFF
    1, // 5  Page 5, map into 0x04000-0x07FFF
    6, // 6  Page 6, map into 0x18000-0x1BFFF
    7  // 6  Page 7, map into 0x1C000-0x1FFFF
};

void update_pmc(unsigned char value)
{
    /*
     For PMC (Primary Memory Control) the following bits are used:
     0-2: RAM bank
     3: Active screen
     4: Active ROM
     5: Lock PMC
     */
    // printf("UPDATE PMC %02x\n", value);
    if (mempage128 & (1<<5)) {
        // Locked, ignore write
        return;
    }
    mempage_offset = rampage_map[ value & 0x7 ];
    if (value & (1<<4)) {
       /* if (!(mempage128&(1<<4))) {
            printf("ROM: switching to ROM bank 1\n");
        }*/
        rom_offset =  0x4000;
    } else {
        /*
        if (mempage128&(1<<4)) {
            printf("ROM: switching to ROM bank 0\n");
        } */
        rom_offset =  0;
    }
    mempage128 = value;
}

static inline unsigned getpage(unsigned short address)
{

    unsigned page = (address >> 14);
    switch (address>>14) {
    case 3:
        page = (unsigned)mempage_offset;
        break;
    default:
        break;
    }
    return page;
}

void writerom(unsigned short address, unsigned char byte)
{
    unsigned real_addr = (address & 0x3FFF)+rom_offset;
    rommemory[real_addr] = byte;
}

void writerom_indexed(unsigned romno, unsigned short address, unsigned char byte)
{
    unsigned real_addr = (address & 0x3FFF)+(romno*0x4000);
    rommemory[(address & 0x3FFF)+(romno*0x4000)] = byte;
}

/*
 Read ROM address, from a CPU perspective.
 This function is marked as weak so it can be overriden
 by ROM extensions.
 */

UCHAR __attribute__((weak)) cpu_readrom(USHORT address)
{
    unsigned real_addr = (address & 0x3FFF)+rom_offset;
    return rommemory[real_addr];
}

/*
 Write ROM address, from a CPU perspective.
 This function is marked as weak so it can be overriden
 by ROM extensions.
 */
void cpu_writerom(USHORT address, UCHAR byte)
{
    /* Nothing */
}


/*
 Read RAM address, from a CPU perspective.
 */
UCHAR cpu_readmem(USHORT address)
{
    unsigned page = getpage(address);
    unsigned memoffset = (unsigned)(address & 0x3FFF) + (page<<14);
    return memory[memoffset];
}

/*
 Write RAM address, from a CPU perspective.
 */
void cpu_writemem(USHORT address, UCHAR byte)
{
    unsigned page = getpage(address);
    unsigned memoffset = (unsigned)(address & 0x3FFF) + ((unsigned)page<<14);
    memory[memoffset] = byte;
    // TODO: check if alternate screen is in use
    if (address >= 0x4000 && address <= 0x8000) {
        video_writebyte(address, byte);
    }
}

/*=========================================================================*
 *                          writebyte_page                                 *
 *=========================================================================*/

void writebyte_page(UCHAR page, USHORT offset, UCHAR value)
{
    unsigned page_real = rampage_map[page];
    page_real <<= 14;

    unsigned memoffset = (unsigned)offset + page_real;
    memory[memoffset] = value;
    if (page == 5) {
        video_writebyte(offset + 0x4000, value);
    }
}

void writebyte_direct(unsigned offset, UCHAR value)
{
    memory[offset] = value;
    if (offset >= 0x4000 && offset < 0x8000)  {
        video_writebyte(offset, value);
    }
}

unsigned getpageaddress(UCHAR page)
{
    unsigned page_real = rampage_map[page];
    page_real <<= 14;
    return page_real;
}

/*=========================================================================*
 *                            writebyte                                    *
 *=========================================================================*/

void writebyte(USHORT addr, UCHAR value)
{
    if(addr < 0x4000) {
        cpu_writerom(addr,value);
        return;
    }
    cpu_writemem(addr, value);
}

/*=========================================================================*
 *                            writeword                                    *
 *=========================================================================*/
void writeword(USHORT addr, USHORT value)
{
    /* Remember: Z80 word is in reversed order */
    writebyte(addr, (UCHAR) (value & 0xff) );
    writebyte(addr + 1, (UCHAR) ( (value >> 8) & 0xff) );
}

#undef readbyte

/*=========================================================================*
 *                            readbyte                                     *
 *=========================================================================*/
UCHAR readbyte(USHORT addr)
{
    if (addr<0x4000)
        return cpu_readrom(addr);
    return cpu_readmem(addr);
}

#undef readword

/*=========================================================================*
 *                            readword                                     *
 *=========================================================================*/
USHORT readword(USHORT addr)
{
    /* Remember: Z80 word is little-endian */
    return( (USHORT) cpu_readmem(addr)  |
           ( ( (USHORT) cpu_readmem(addr+1) ) << 8 ) );
}


#undef Getnextword

/*=========================================================================*
 *                            Getnextword                                  *
 *=========================================================================*/
//USHORT Getnextword()
//{
	/* Remember: Z80 word is in reversed order */
//	PC += 2;
//	return(readword((USHORT)PC - 2));
//}

// see video.c for readbyte, since it supports the video logic

/* EOF: Mem.c */
