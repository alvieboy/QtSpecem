
/* InitMem.c : Initialize Spectrum memory.
 *
 * Copyright 1991-2019 Rui Fernando Ferreira Ribeiro.
 *
 */

//#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <process.h>
#include <stdlib.h>
#include "../h/env.h"

//char szROMPath[260];
//char szHELPath[260];

void init_emul()
{
    USHORT i = 0;
    //FILE * f;

    /* Open Z80 emulation with 64Kb of RAM */
    Init_Z80Emu();
    do_reset();

}

/* EOF: InitMem.c */
