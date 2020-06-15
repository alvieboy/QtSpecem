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

extern "C" {
#include "z80core/iglobal.h"
#include "z80core/z80.h"
    static volatile int nmi_pending = 0;
    static volatile const uint8_t *nmi_rom = NULL;
    void save_sna(const char * file_name);
    extern void open_sna(const char*);
    extern uint16_t read_DE();
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
        if (audio_event_queue.size()) {
            unsigned long long expires = audio_event_queue.front();
            if (expires > ticks) {
                audio_event_queue.pop_front();
                toggle_audio();
            }
        }
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
    while (datalen--) {
        printf(" %02x", *data);
        m_tapfifo.push_back(*data++);
    }
    emit tapDataReady();
    printf(" ]\n");
}

void InterfaceZ::fpgaWriteTapFifoCmd(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    printf("TAP FIFO write (cmd): [");
    while (datalen--) {
        printf(" %02x", *data);
        m_tapfifo.push_back((*data++)| 0x0100);
    }
    emit tapDataReady();
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



