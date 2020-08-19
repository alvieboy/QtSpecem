#ifndef __TAPE_H__
#define __TAPE_H__

#include <inttypes.h>

enum tapstate {
    TAP_IDLE,
    TAP_CMDDATA,
    TAP_BLOCKLEN,
    TAP_TYPE,
    TAP_PLAY
};

#define DEFAULT_PILOT 2168
#define DEFAULT_SYNC0 667
#define DEFAULT_SYNC1 735
#define DEFAULT_LOGIC0 885
#define DEFAULT_LOGIC1 1710
#define DEFAULT_PILOT_HEADER_LEN 8063
#define DEFAULT_PILOT_DATA_LEN 3223
#define DEFAULT_GAP 2000

class TapePlayer
{
public:
    TapePlayer();
    void handleStreamData(uint16_t data);
protected:
    void sendPilot();
    void sendSync();
    void handleData(uint8_t data);
    void handleCommand(uint8_t data);
    void gotType(uint8_t);
    void reset();
    void gap(uint32_t val_ms);
    void sendByte(uint8_t value, uint8_t count = 8);
    void sendBit(uint8_t value);
    void push(unsigned delta);

private:
    uint16_t pilot_len;
    uint16_t sync0_len;
    uint16_t sync1_len;
    uint16_t logic0_len;
    uint16_t logic1_len;
    uint32_t gap_len;
    uint16_t pilot_header_len;
    uint16_t pilot_data_len;
    uint16_t repeat;
    uint8_t dptr;
    uint8_t dbuf[2];
    bool len_external;
    uint32_t blocklen;
    uint8_t type;
    uint8_t cmd;
    bool playing;
    uint8_t lastbytelen;
    tapstate state;
    bool standard;
};


#endif
