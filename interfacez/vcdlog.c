#include "vcdlog.h"
#include <stdio.h>
#include <inttypes.h>

static FILE *vcdfile = NULL;

static uint8_t audioval = 0;

void vcdlog_audio(unsigned long long tick)
{
    audioval = !audioval;
    if (vcdfile) {
        fprintf(vcdfile, "#%lld\n"
                "b%d a\n", tick, audioval);

    }
}

void vcdlog_init(const char *filename)
{
    vcdfile = fopen(filename,"w");
    if (vcdfile!=NULL) {
        fprintf(vcdfile,
                "$date\n"
                "Tue Dec  4 19:27:08 2020\n"
                "$end\n"
                "$version\n"
                "GHDL v0\n"
                "$end\n"
                "$timescale\n"
                "28.571429 ns\n"
                "$end\n"
                "$var wire 1 a AUDIO $end\n"
                "$enddefinitions $end\n"
               );
    }
}
