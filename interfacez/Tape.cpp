#include "Tape.h"
#include <stdio.h>

extern "C" void audio_push(unsigned delta);
extern "C" void audio_start(void);
extern "C" void audio_pause(unsigned long);

void log_audio(unsigned long long);
void log_init(const char *filename);

void TapePlayer::push(unsigned delta)
{
    if (!playing) {
        playing = true;
        audio_start();
        log_init("audio.vcd");
    }
    audio_push(delta);
}
void TapePlayer::handleStreamData(uint16_t data)
{
    if (data & 0x100) {
        handleCommand(data & 0xff);
    } else {
        handleData(data & 0xff);
    }
}

void TapePlayer::sendSync()
{
    push(sync0_len);
    push(sync1_len);
}

void TapePlayer::handleData(uint8_t data)
{
    blocklen--;
    if (blocklen==0) {
        sendByte(data, lastbytelen);
    } else {
        sendByte(data);
    }
}

#define CMDDEBUG(x...) do { printf("DBG: "); printf(x); printf("\n"); } while (0);

#define TAP_INTERNALCMD_SET_PULSE0 0x80
#define TAP_INTERNALCMD_SET_PULSE1 0x81
#define TAP_INTERNALCMD_GAP        0x82
#define TAP_INTERNALCMD_SET_DATALEN1 0x83
#define TAP_INTERNALCMD_SET_DATALEN2 0x84
#define TAP_INTERNALCMD_SET_REPEAT 0x85 /* Follows pulse repeat */
#define TAP_INTERNALCMD_PLAY_PULSE 0x86 /* Follows pulse t-states, repeats for REPEAT */
#define TAP_INTERNALCMD_FLUSH 0x87 /* Ignored. Use to flush SPI */

void TapePlayer::handleCommand(uint8_t data)
{
    uint16_t val16;
    switch (state) {
    case TAP_IDLE:

        if (data & 0x80) {
            dptr = 0;
            cmd  = data;
            state = TAP_CMDDATA;
        } 
        break;
    case TAP_CMDDATA:
        dbuf[dptr++] = data;
        if (dptr==2) {
            val16 = ((uint16_t)dbuf[0]) + (uint16_t(dbuf[1])<<8);
            switch (cmd) {
            case TAP_INTERNALCMD_SET_PULSE0:
                CMDDEBUG("Set pulse0 to %d\n", val16);
                logic0_len = val16;
                break;
            case TAP_INTERNALCMD_SET_PULSE1:
                CMDDEBUG("Set pulse1 to %d\n", val16);
                logic1_len = val16;
                break;
            case TAP_INTERNALCMD_GAP:
                CMDDEBUG("Play gap %d ms\n", val16);
                gap(val16);
                break;
            case TAP_INTERNALCMD_SET_DATALEN1:
                CMDDEBUG("Set datalen %d\n", val16);
                blocklen = ((int32_t)val16)+1;
                break;
            case TAP_INTERNALCMD_SET_DATALEN2:
                blocklen += ((uint32_t)val16&0xff)<<16;
                lastbytelen = 8-((val16>>8) & 0x7);
                CMDDEBUG("Set datalen2 %d lastbyte %d\n", blocklen, lastbytelen);
                break;
            case TAP_INTERNALCMD_SET_REPEAT:
                CMDDEBUG("Set repeat %d\n", val16);
                repeat = val16;
                break;
            case TAP_INTERNALCMD_PLAY_PULSE: {
                CMDDEBUG("Pulse %d (repeat %d)", val16, repeat);
                unsigned i;
                for (i=0; i<=repeat;i++) {
                    push(val16);
                }
            }
            break;

            default:
                break;
            }
            dptr = 0;
            state = TAP_IDLE;

        }
    default:
        break;
    }
}

void TapePlayer::sendBit(uint8_t value)
{
    if (value) {
        push(logic1_len);
        push(logic1_len);
    } else {
        push(logic0_len);
        push(logic0_len);
    }
}

void TapePlayer::gap(uint32_t val_ms)
{
    printf("GAP %d ms (%ld ticks)", val_ms, (unsigned long)val_ms * 3500UL);
    audio_pause( val_ms * 3500 );
}

void TapePlayer::sendByte(uint8_t value, uint8_t bytelen)
{
//    printf("BYTE %02x (%d)\n", value, bytelen);

    while (bytelen--) {
        sendBit(value & 0x80);
        value<<=1;
    }
}

void TapePlayer::reset()
{
    pilot_len = DEFAULT_PILOT;
    sync0_len = DEFAULT_SYNC0;
    sync1_len = DEFAULT_SYNC1;
    logic0_len = DEFAULT_LOGIC0;
    logic1_len = DEFAULT_LOGIC1;
    gap_len = DEFAULT_GAP;
    pilot_header_len = DEFAULT_PILOT_HEADER_LEN;
    pilot_data_len = DEFAULT_PILOT_HEADER_LEN;
    len_external = false;
    standard = true;
    lastbytelen = 8;
};

TapePlayer::TapePlayer()
{
    reset();
    state = TAP_IDLE;
    playing = false;
}

