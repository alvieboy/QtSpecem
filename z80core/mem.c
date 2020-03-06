/* Mem.c: Z80 memory - basic support routines.
 *
 * Copyright 1991-2019 Rui Fernando Ferreira Ribeiro.
 *
 */

//#include "../h/quirks.h"
#include "env.h"

//static UCHAR inited = 0;


#if !defined(NDEBUG)
extern UCHAR running;
#endif

/*=========================================================================*
 *                            writebyte                                    *
 *=========================================================================*/
//void writebyte(USHORT addr, UCHAR value) {
//	 UINT hiAddr = addr >> 14;
//
//	 if (hiAddr) {               // Not in ROM?
//		  *(mem+addr)  = value;
//	 }
//}

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
    /* definide as a macro at z80.h */
    if (addr < 0x4000)
        return readROM(addr);
    return readRAM(addr);
}

#undef readword

/*=========================================================================*
 *                            readword                                     *
 *=========================================================================*/
USHORT readword(USHORT addr)
{
    /* Remember: Z80 word is in reversed order */
    return( ((USHORT) readbyte(addr) ) |
           ( ( (USHORT) readbyte(addr+1) ) << 8 ) );
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
