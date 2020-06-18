#include "interfacez.h"
#include <QDebug>
#include <QHostAddress>
#include <QTcpSocket>
#include <QObject>
#include <QAbstractSocket>
#include <QFile>
#include <QByteArray>
#include "sna_relocs.h"
#include "SnaFile.h"

/* FPGA IO ports */

#define FPGA_PORT_SCRATCH0	(0x23)
#define FPGA_PORT_SCRATCH1	(0x27)
#define FPGA_PORT_CMD_FIFO_STATUS (0x2B)
#define FPGA_PORT_CMD_FIFO_DATA (0x67)
#define FPGA_PORT_RESOURCE_FIFO_STATUS (0x2F)
#define FPGA_PORT_RESOURCE_FIFO_DATA (0x33)
#define FPGA_PORT_RAM_ADDR_0 (0x37)
#define FPGA_PORT_RAM_ADDR_1 (0x3B)
#define FPGA_PORT_RAM_ADDR_2 (0x3F)
#define FPGA_PORT_RAM_DATA (0x63)

/* Trigger flags */

#define FPGA_FLAG_TRIG_RESOURCEFIFO_RESET (1<<0)
#define FPGA_FLAG_TRIG_FORCEROMONRETN     (1<<1)
#define FPGA_FLAG_TRIG_FORCEROMCS_ON      (1<<2)
#define FPGA_FLAG_TRIG_FORCEROMCS_OFF     (1<<3)
#define FPGA_FLAG_TRIG_INTACK             (1<<4)
#define FPGA_FLAG_TRIG_CMDFIFO_RESET      (1<<5)
#define FPGA_FLAG_TRIG_FORCENMI_ON        (1<<6)
#define FPGA_FLAG_TRIG_FORCENMI_OFF       (1<<7)

#define FPGA_FLAG_RSTSPECT (1<<1)

static QList<unsigned long long> audio_event_queue;

static void log_audio(unsigned long long);
static void log_init(const char *filename);

extern "C" {
#include "z80core/iglobal.h"
#include "z80core/z80.h"
    static volatile int nmi_pending = 0;
    static volatile const uint8_t *nmi_rom = NULL;
    void save_sna(const char * file_name);
    extern void open_sna(const char*);
    extern uint16_t read_DE();

    extern unsigned long long get_clock_ticks_since_startup(void);

    void execute_if_running()
    {
        if (nmi_pending) {
            nmi_pending=0;
            //open_sna("input.sna");
            do_nmi_int();
            if (nmi_rom) {
                // test
                set_current_rom((const unsigned char*)nmi_rom);
            }
        }
        execute();
    }

    void retn_called_hook()
    {
        // Upon retn, restore ROM.
        printf("RETN called, restoring stock ROM\n");
        set_current_rom(NULL);

       // save_sna("snadump.sna");
    }
    void trigger_nmi(const uint8_t *rom)
    {
        nmi_rom = rom;
        nmi_pending = 1;
    }
    void reset_spectrum() {
        set_current_rom(NULL);
        do_reset();
    }

    extern void toggle_audio();

    void insn_executed(unsigned long long ticks) {
        //printf("%lld\n", ticks);
        if (audio_event_queue.size()) {
            unsigned long long expires = audio_event_queue.front();
            if (expires <= ticks) {
                //printf("Audio event %lld %lld\n", expires, ticks);
                audio_event_queue.pop_front();
                toggle_audio();
            }
        }
    }

    unsigned long long startpos;

    void audio_start()
    {
        startpos = get_clock_ticks_since_startup();
        printf("Start play %lld\n", startpos);
    }

    void audio_push(unsigned delta)
    {
        startpos += delta;
        //printf("Pulse %u\n", delta);
        audio_event_queue.push_back( startpos );
        log_audio(startpos);
    }

    void audio_pause(unsigned long delta)
    {
        startpos += delta;
    }

    bool audio_has_data()
    {
        return audio_event_queue.size() > 0;
    }
}

static int strtoint(const char *str, int *dest)
{
    char *endptr;
    int val = strtoul(str,&endptr, 0);
    if (endptr) {
        if (*endptr=='\0') {
            *dest = val;
            return 0;
        }
    }
    return -1;
}



void InterfaceZ::hdlc_writer(void *userdata, const uint8_t ch)
{
    Client *c = static_cast<Client*>(userdata);

    c->m_txarray.append((char)ch);
}

void InterfaceZ::hdlc_flusher(void *userdata)
{
    Client *c = static_cast<Client*>(userdata);

    c->s->write(c->m_txarray);
    c->m_txarray.clear();
}


UCHAR interfacez__ioread(void*user,UCHAR address)
{
    return static_cast<InterfaceZ*>(user)->ioread(address);
}

void interfacez__iowrite(void*user,UCHAR address, UCHAR value)
{
    static_cast<InterfaceZ*>(user)->iowrite(address, value);
}

void InterfaceZ::hdlcDataReady(void *user, const uint8_t *data, unsigned len)
{
    Client *c = static_cast<Client*>(user);
    c->intf->hdlcDataReady(c, data,len);
}

InterfaceZ::InterfaceZ()
{
    customromloaded = false;
}

int InterfaceZ::init()
{
    int r  = register_expansion_port(0x01,
                                     0x01,
                                     &interfacez__ioread,
                                     &interfacez__iowrite,
                                     this
                                    );
    m_fpgasocket = new QTcpServer();

    connect(m_fpgasocket, &QTcpServer::newConnection, this, &InterfaceZ::newConnection);

    if (!m_fpgasocket->listen(QHostAddress::Any, 8007)) { // QTcpSocket::ReuseAddressHint
        qDebug()<<"Cannot listen";
        return -1;
    }

    m_sna_rom_size = -1;

    return r;
}

void InterfaceZ::newConnection()
{
    QTcpSocket *s = m_fpgasocket->nextPendingConnection();

    s->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    Client *c = new Client(this);

    hdlc_encoder__init(&c->m_hdlc_encoder, &hdlc_writer, &hdlc_flusher, c);

    c->s = s;

    connect(s, &QTcpSocket::readyRead, this, [this,c](){ this->readyRead(c); });
    connect(s, qOverload<QAbstractSocket::SocketError>(&QAbstractSocket::error),
            this,[this,c](QAbstractSocket::SocketError error){this->socketError(c, error);});


    hdlc_decoder__init(&c->m_hdlc_decoder,
                       c->m_hdlcrxbuf,
                       sizeof(c->m_hdlcrxbuf),
                       &InterfaceZ::hdlcDataReady,
                       c);
    m_clients.push_back(c);
}


void InterfaceZ::socketError(InterfaceZ::Client *c, QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    qDebug()<<"Socket error"<<c->s->error();
    c->s->close();

    int i = m_clients.indexOf(c);
    if (i != -1) {
        m_clients.removeAt(i);
    }
    delete(c);
}

UCHAR InterfaceZ::ioread(UCHAR address)
{
    uint8_t val = 0xff;


    switch (address) {
    case 0x05:
        val = 0x39; //Bg
        break;

    case FPGA_PORT_CMD_FIFO_STATUS: // Cmd fifo status
        if (m_cmdfifo.size()>=32) {
            val = 0x01;
        } else {
            val = 0x00;
        }
        break;
    case FPGA_PORT_RESOURCE_FIFO_STATUS:
        if (m_resourcefifo.empty()) {
            val = 0x01;
        } else {
            val = 0x00;
        }
        break;

    case FPGA_PORT_RESOURCE_FIFO_DATA:
        if (m_resourcefifo.empty()) {
            val = 0x00;
        } else {
            val = m_resourcefifo.front();
            m_resourcefifo.pop_front();
        }

        break;

    case FPGA_PORT_RAM_DATA:
        printf("return RAM[0x%06x] = 0x%02x\n", extramptr, extram[extramptr]);
        val = extram[extramptr++];
        break;

    case 0x1F: // Joy port
        val = 0x00;
        break;
    }

    return val;
}



void InterfaceZ::iowrite(UCHAR address, UCHAR value)
{
    switch (address) {
    case FPGA_PORT_CMD_FIFO_DATA: // Cmd fifo
        printf("CMD FIFO write: 0x%04x 0x%02x\n", address, value);
        if (m_cmdfifo.size()<32) {
            m_cmdfifo.push_back(value);
            cmdFifoWriteEvent();
        }
        break;
    case FPGA_PORT_RAM_ADDR_0:
        // Address LSB
        extramptr &= 0xFFFF00;
        extramptr |= value;
        break;

    case FPGA_PORT_RAM_ADDR_1:
        // Address hSB
        extramptr &= 0xFF00FF;
        extramptr |= ((uint32_t)value)<<8;
        printf("EXT ram pointer: %06x\n", extramptr);
        break;

    case FPGA_PORT_RAM_ADDR_2:
        // Address MSB
        extramptr &= 0x00FFFF;
        // We only use 1 banks.
        value &= 0x1;

        extramptr |= ((uint32_t)value)<<16;
        printf("EXT ram pointer: %06x\n", extramptr);
        break;

    case FPGA_PORT_RAM_DATA:
        printf("RAM[0x%06x] = 0x%02x\n", extramptr, value);
        extram[extramptr++] = value;
        if (extramptr>=sizeof(extram)) {
            extramptr=0;
        }
        break;

    }
}



void InterfaceZ::readyRead(InterfaceZ::Client *c)
{
    uint8_t rxbuf[256];

//    qDebug()<<"Reading data";
    int len;

    do {
        len = c->s->read( (char*)rxbuf, sizeof(rxbuf));

        if (len<0) {
            return;
        }

        hdlc_decoder__append_buffer(&c->m_hdlc_decoder, rxbuf, len);

    } while (len);
}



static int do_trace=0;
void rom_access_hook(USHORT address, UCHAR data)
{
    if (do_trace) {
        printf("ROM: %04x: %02x\n", address, data);
    }
}
void enable_trace()
{
    do_trace=1;
}

void InterfaceZ::onNMI()
{
    printf(" ***** NMI ***** (custom: %s)\n", customromloaded?"yes":"no");
    //trigger_nmi(customromloaded ? customrom : NULL);
    for (auto i: m_clients) {
        i->gpioEvent(PIN_NUM_SWITCH);
    }
    
}

void InterfaceZ::loadCustomROM(const char *name)
{
    QFile file(name);
    QByteArray data;
    const char * p;
    printf("Loading custom ROM from %s\n", name);
    if(file.open(QIODevice::ReadOnly)){
        data=file.readAll();
        file.close();
        p=data;
        for (int i=0; i < 16384 ; i++)
            *(customrom+i) = *(p++);
    } else {
        printf("Erorr loading custom ROM from %s\n", name);

        return;
    }

    customromloaded = true;
}

void InterfaceZ::hdlcDataReady(Client *c, const uint8_t *data, unsigned datalen)
{
    uint8_t cmd = data[0];
    uint8_t *txbuf_complete = (uint8_t*)malloc(datalen);

    //printf("CMD: 0x%02x len %d\n", cmd, datalen);

    data++;
    datalen--;

    uint8_t *txbuf = &txbuf_complete[1]; // Skip command location.
    try {
    switch(cmd) {
    case FPGA_CMD_READ_STATUS:
        fpgaReadStatus(data, datalen, txbuf);
        break;
    case FPGA_CMD_READ_VIDEO_MEM:
        break;
    case FPGA_CMD_READ_PC:
        break;
    case FPGA_CMD_READ_EXTRAM:
        fpgaReadExtRam(data,datalen,txbuf);
        break;
    case FPGA_CMD_WRITE_EXTRAM:
        fpgaWriteExtRam(data,datalen,txbuf);
        break;
    case FPGA_CMD_READ_USB:
        break;
    case FPGA_CMD_WRITE_USB:
        break;
    case FPGA_CMD_WRITE_ROM:
        fpgaWriteROM(data, datalen, txbuf);
        break;
    case FPGA_CMD_WRITE_RES_FIFO:
        fpgaWriteResFifo(data,datalen,txbuf);
        break;
    case FPGA_CMD_WRITE_TAP_FIFO:
        fpgaWriteTapFifo(data,datalen,txbuf);
        break;
    case FPGA_CMD_WRITE_TAP_FIFO_CMD:
        fpgaWriteTapFifoCmd(data,datalen,txbuf);
        break;
    case FPGA_CMD_GET_TAP_FIFO_USAGE:
        fpgaGetTapFifoUsage(data,datalen,txbuf);
        break;
    case FPGA_CMD_SET_FLAGS:
        fpgaSetFlags(data, datalen, txbuf);
        break;
    case FPGA_CMD_SET_REGS32:
        fpgaSetRegs32(data,datalen,txbuf);
        break;
    case FPGA_CMD_GET_REGS32:
        fpgaGetRegs32(data,datalen,txbuf);
        break;
    case FPGA_CMD_READ_CMDFIFO_DATA:
        fpgaReadCmdFifo(data,datalen,txbuf);
        break;
    case FPGA_CMD_READID1: /* Fall-through */
    case FPGA_CMD_READID2:
        fpgaCommandReadID(data, datalen, txbuf);
        break;
    }
    } catch (std::exception &e) {
        fprintf(stderr,"Cannot parse SPI block: %s\n", e.what());
    }


    hdlc_encoder__begin(&c->m_hdlc_encoder);
    uint8_t scmd = 0x01;
    hdlc_encoder__write(&c->m_hdlc_encoder, &scmd, sizeof(scmd));
    hdlc_encoder__write(&c->m_hdlc_encoder, txbuf_complete, datalen+1);
    hdlc_encoder__end(&c->m_hdlc_encoder);
    free(txbuf_complete);
}

void InterfaceZ::fpgaCommandReadID(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(data);
    Q_UNUSED(datalen);
    txbuf[0] = 0xA5;
    txbuf[1] = 0xA5;
    txbuf[2] = 0x10;
    txbuf[3] = 0x03;
}



#define FPGA_STATUS_DATAFIFO_EMPTY (1<<0)
#define FPGA_STATUS_RESFIFO_FULL   (1<<1)
#define FPGA_STATUS_RESFIFO_QFULL  (1<<2)
#define FPGA_STATUS_RESFIFO_HFULL  (1<<3)
#define FPGA_STATUS_RESFIFO_QQQFULL  (1<<4)
#define FPGA_STATUS_CMDFIFO_EMPTY  (1<<5)

#define RESFIFO_SIZE 1024

void InterfaceZ::fpgaReadStatus(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(data);

    if (datalen<1)
        throw DataShortException();

    uint8_t status = 0;
    if (m_cmdfifo.empty()) {
        status |= FPGA_STATUS_CMDFIFO_EMPTY;
    }

    if (m_resourcefifo.size()>=RESFIFO_SIZE/4) {
        status |= FPGA_STATUS_RESFIFO_QFULL;
    }
    if (m_resourcefifo.size()>=RESFIFO_SIZE/2) {
        status |= FPGA_STATUS_RESFIFO_HFULL;
    }

    if (m_resourcefifo.size()>=(RESFIFO_SIZE*2)/3) {
        status |= FPGA_STATUS_RESFIFO_QQQFULL;
    }
    if (m_resourcefifo.size()>=(RESFIFO_SIZE*2)/3) {
        status |= FPGA_STATUS_RESFIFO_FULL;
    }


    txbuf[0] = status;
}

void InterfaceZ::fpgaWriteROM(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    // Extract offset
    uint16_t offset;
    data = extractbe16(data,datalen,offset);

    printf("ROM: chunk %d offset 0x%04x\n", datalen, offset);
    memcpy(&customrom[offset], data, datalen);
    customromloaded = true;

}

void InterfaceZ::fpgaSetFlags(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);

    if (datalen<3)
        throw DataShortException();

    uint16_t old_flags = fpga_flags;

    fpga_flags = ((uint16_t)data[0]) | (data[2]<<8);

    if ( (old_flags & FPGA_FLAG_RSTSPECT)
        && !(fpga_flags & FPGA_FLAG_RSTSPECT) ) {
        reset_spectrum();
    }


    // Triggers
    
    if (data[1] & FPGA_FLAG_TRIG_RESOURCEFIFO_RESET) {
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCEROMONRETN) {
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCEROMCS_ON) {
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCEROMCS_OFF) {
    }
    if (data[1] & FPGA_FLAG_TRIG_INTACK) {
    }
    if (data[1] & FPGA_FLAG_TRIG_CMDFIFO_RESET) {
        m_cmdfifo.clear();
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCENMI_ON) {
        trigger_nmi(customromloaded? customrom:NULL);
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCENMI_OFF) {
    }
}

void InterfaceZ::fpgaReadExtRam(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    uint32_t offset;
    data = extractbe24(data,datalen,offset);

    if((offset+datalen) > sizeof(extram)) {
        printf("Attempt to read outside extram, offset 0x%08x len %d\n", offset, datalen);
        return;
    }
    memcpy( &txbuf[3], &extram[offset], datalen);
}

void InterfaceZ::fpgaWriteExtRam(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    uint32_t offset;
    data = extractbe24(data,datalen,offset);

    if((offset+datalen) > sizeof(extram)) {
        printf("Attempt to read outside extram, offset 0x%08x len %d\n", offset, datalen);
        return;
    }

    memcpy( &extram[offset], data, datalen);

}
void InterfaceZ::fpgaWriteResFifo(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    printf("Resource FIFO write: [");
    while (datalen--) {
        printf(" %02x", *data);
        m_resourcefifo.push_back(*data++);
    }
    printf(" ]\n");
}

void InterfaceZ::fpgaWriteTapFifo(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    printf("TAP FIFO write: [");
    int i;
    for (i=0;i<datalen;i++) {
        printf(" %02x", data[i]);
    }
    printf(" ]\n");

    while (datalen--) {
        m_player.handleStreamData(*data);
        data++;
    }
}

void InterfaceZ::fpgaWriteTapFifoCmd(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    printf("TAP FIFO write (cmd): [");
    while (datalen--) {
        m_player.handleStreamData(*data | 0x100);
        printf(" %02x", *data);
        data++;
    }
    printf(" ]\n");
}

void InterfaceZ::fpgaGetTapFifoUsage(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(data);
    Q_UNUSED(datalen);

    uint16_t usage = m_tapfifo.size();
    if (usage>1023) {
        usage |= 0x8000;
    }
    txbuf[0] = usage >> 8;
    txbuf[1] = usage & 0xff;
}


void InterfaceZ::fpgaSetRegs32(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    uint32_t regdata;
    uint8_t regnum;
    data = extractu8(data, datalen, regnum);
    data = extractbe32(data, datalen, regdata);
    if (regnum<32) {
        regs[regnum] = regdata;
    }
}

void InterfaceZ::fpgaGetRegs32(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    uint32_t regdata;
    uint8_t regnum;
    data = extractu8(data, datalen, regnum);

    if (regnum<32) {
        regdata = regs[regnum];
        txbuf[2] = (regdata>>24);
        txbuf[3] = (regdata>>16);
        txbuf[4] = (regdata>>8);
        txbuf[5] = (regdata);
    }
}

void InterfaceZ::fpgaReadCmdFifo(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(data);
    if (datalen<2)
        throw DataShortException();

    bool empty = m_cmdfifo.empty();
    if (empty) {
        txbuf[0] = 0xFF;
    } else {
        txbuf[0] = 0x00;
        uint8_t v = m_cmdfifo.front();
        printf("Read CMD fifo: %02x\n", v);
        txbuf[1] = v;
        m_cmdfifo.pop_front();
    }
}


void InterfaceZ::Client::gpioEvent(uint8_t v)
{
    hdlc_encoder__begin(&m_hdlc_encoder);
    uint8_t cmd[2]= { 0x00, v };
    hdlc_encoder__write(&m_hdlc_encoder, &cmd, sizeof(cmd));
    hdlc_encoder__end(&m_hdlc_encoder);
}


void InterfaceZ::cmdFifoWriteEvent()
{
    for (auto c: m_clients) {
        c->gpioEvent(PIN_NUM_CMD_INTERRUPT);
    }
}



void TapePlayer::handleStreamData(uint16_t data)
{
    if (data & 0x100) {
        handleCommand(data & 0xff);
    } else {
        handleData(data & 0xff);
    }
}

void TapePlayer::sendPilot()
{
    unsigned i;
    for (i=0;i<pilot_header_len*2;i++) {
        audio_push(pilot_len);
    }
}

void TapePlayer::sendSync()
{
    audio_push(sync0_len);
    audio_push(sync1_len);
}

void TapePlayer::gotType(uint8_t data)
{
    type = data;
    blocklen--;
    if (!playing) {
        playing = true;
        audio_start();
        log_init("audio.vcd");
    }
    if (standard) {
        sendPilot();
        sendSync();
    }
    if (blocklen>1)
        sendByte(type);
    else
        sendByte(type, lastbytelen);

    printf("TAP: play chunk size %d\n", blocklen);
}
void TapePlayer::handleData(uint8_t data)
{
    switch (state) {
    case TAP_IDLE:
        if (!len_external) {
            blocklen = data;
            printf("Block len0: [ %02x ]\n", data);
            state = TAP_BLOCKLEN;
        } else {
            gotType(data);
            state = TAP_PLAY;
        }
        break;
    case TAP_BLOCKLEN:
        blocklen += ((uint32_t)data)<<8;
        printf("Block len1: [ %04x ]\n", blocklen);
        state = TAP_TYPE;
        break;

    case TAP_TYPE:
        gotType(data);
        state = TAP_PLAY;
        break;

    case TAP_PLAY:
        blocklen--;
        if (blocklen==0) {
            //printf("Data byte: [ %02x ] (%d)\n", data, lastbytelen);
            sendByte(data, lastbytelen);
            //if (standard) {
            //    gap( 10 );
            //} else {
                gap ( gap_len );
            //}
            state = TAP_IDLE;
        } else {
            //printf("Data byte: [ %02x ]\n", data);
            sendByte(data);
        }
        break;
    }
}

#define CMDDEBUG(x...) do { printf("DBG: "); printf(x); printf("\n"); } while (0);

void TapePlayer::handleCommand(uint8_t data)
{
    uint16_t val16;
    switch (state) {
    case TAP_IDLE:

        if (!(data & 0x80)) {
            dptr = 0;
            cmd = data & 0x7F;
            //CMDDEBUG("%02x: Need argument", data);
            state = TAP_CMDDATA;
        } else {
            // Single byte command
            switch (data) {
            case 0x80:
                CMDDEBUG("Resetting values");
                reset();
                break;
            case 0x82:
                CMDDEBUG("Setting LEN to external");
                len_external=true;
                break;
            case 0x83:
                CMDDEBUG("Setting LEN to internal");
                len_external=false;
                break;
            case 0x84:
                CMDDEBUG("Setting PURE");
                standard=false;
                break;
            case 0x85:
                CMDDEBUG("Setting STANDARD");
                standard=true;
                break;
            }
        }
        break;
    case TAP_CMDDATA:
        dbuf[dptr++] = data;
        if (dptr==2) {
            val16 = ((uint16_t)dbuf[0]) + (uint16_t(dbuf[1])<<8);
            switch (cmd) {
            case 0x00: pilot_len = val16; break;
            case 0x01: sync0_len = val16; break;
            case 0x02: sync1_len = val16; break;
            case 0x03: logic0_len = val16; break;
            case 0x04: logic1_len = val16; break;
            case 0x08: pilot_header_len = val16; break;
            case 0x09: pilot_data_len = val16; break;
            case 0x0A: gap_len = val16; break;
            case 0x0B: blocklen = val16; break;
            case 0x0C: blocklen += ((uint32_t)val16&0xff)<<16;
                       lastbytelen = 8-((val16>>8) & 0x7);
                       break;
            case 0x0e: repeat = val16; break;
            case 0x0d: {
                // Play pulse data.
                CMDDEBUG("Pulse %d (repeat %d)", val16, repeat);
                unsigned i;
                for (i=0; i<=repeat;i++) {
                    audio_push(val16);
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
        audio_push(logic1_len);
        audio_push(logic1_len);
    } else {
        audio_push(logic0_len);
        audio_push(logic0_len);
    }
}

void TapePlayer::gap(uint32_t val_ms)
{
    printf("GAP %d ms (%ld ticks)", val_ms, (unsigned long)val_ms * 3500UL);
    audio_pause( val_ms * 3500 );
}

void TapePlayer::sendByte(uint8_t value, uint8_t bytelen)
{
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

static FILE *vcdfile = NULL;

static uint8_t audioval = 0;

void log_audio(unsigned long long tick)
{
    audioval = !audioval;
    if (vcdfile) {
        fprintf(vcdfile, "#%lld\n"
                "b%d a\n", tick, audioval);

    }
}

static void log_init(const char *filename)
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

/*
 13 00 00 00 73 6b 6f 6f 6c 64 61 7a 65 20 1b 00 0a 00 1b 00 44

 1d 00 ff 00
 0a 05 00 ef 22 22 af 0d 00 14 0e 00 f9 c0 32 33 32 39 36 0e 00 00 00 5b 00 0d fa 13 00 00 03 73 6b 6f 6f 

/*
 13 00 00 00 73 6b 6f 6f 6c 64 61 7a 65 20 1b 00 0a 00 1b 00 44 1d 00 ff 00

 0a 05 00 ef 22 22 af  |....D........"".|
00000020  0d 00 14 0e 00 f9 c0 32  33 32 39 36 0e 00 00 00  |.......23296....|
00000030  5b 00 0d fa 13 00 00 03  73 6b 6f 6f 6c 64 61 7a  |[.......skooldaz|
00000040  65 20 12 1b 00 40 00 00  04 14 1b ff ff ff ff ff  |e ...@..........|
00000050  ff ff ff ff ff fe 0f ff  ff f1 ff ff e0 00 78 ff  |..............x.|
00000060  ff 82 00 0f 00 1f f8 00  f0 fd 55 55 ff ff ff ff  |..........UU....|
00000070  ff ff ff ff fa 0f ff ff  ff f8 ff d5 f0 00 7f f8  |................|
00000080  03 f0 00 06 00 fe 07 07  c0 85 55 55 ff ff ff ff  |..........UU....|
00000090  ff ff ff f4 0f ff ff ff  ff fe 7f d5 78 00 7f f8  |............x...|
000000a0  05 10 00 1a e0 01 c2 ff  c0 85 55 55 ff ff ff ff  |..........UU....|
000000b0  ff ff e8 0f ff ff ff ff  ff ff 3f f5 7c 00 7f ff  |..........?.|...|
000000c0  ea aa bb aa ae 00 7f ff  c0 85 55 55 ff ff ff ff  |..........UU....|
000000d0  ff d0 0f ff ff ff ff ff  ff ff 8f f5 5f 00 7f ff  |............_...|
000000e0  ea aa ae ae ae b0 1f ff  00 85 55 55 ff ff ff ff  |..........UU....|
000000f0  a0 0f ff fc ff ff ff ff  ff ff c7 fd 5f 00 3f ff  |............_.?.|
00000100  fa aa af ea ab fe ff e0  00 85 55 55 ff ff fe 40  |..........UU...@|
00000110  0f ff ff ff 3f ff ff e1  e6 7f e3 fd 57 ff ff ff  |....?.......W...|
00000120  fe aa af ea ab fb ff ff  ff 85 55 55 ff fc 40 0f  |..........UU..@.|
00000130  ff ff f9 fb c0 ff ff 79  e3 bf f9 ff 57 c0 03 ff  |.......y....W...|
00000140  fe aa af ea ab ff f8 00  00 85 55 55 ff ff ff ff  |..........UU....|
00000150  ff ff ff ff ff f4 1f ff  ff f1 ff aa e0 00 78 ff  |..............x.|
00000160  ff fd 00 07 00 0f f8 00  f0 86 aa aa ff ff ff ff  |................|
00000170  ff ff ff ff f4 1f ff ff  ff f8 ff aa f0 00 7f f0  |................|
00000180  00 d0 00 0f 00 f8 03 07  c0 86 aa aa ff ff ff ff  |................|
00000190  ff ff ff e8 1f ff ff ff  ff fe 7f ea fc 00 7f fc  |................|
000001a0  0f 78 00 3f e0 00 e3 ff  c0 86 aa aa ff ff ff ff  |.x.?............|
000001b0  ff ff d0 1f ff ff ff ff  ff ff 3f ea be 00 7f ff  |..........?.....|
*/