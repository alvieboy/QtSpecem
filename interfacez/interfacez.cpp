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
#include <QPushButton>
#include <unistd.h>

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

InterfaceZ *InterfaceZ::self = NULL;

extern "C" {
#include "z80core/iglobal.h"
#include "z80core/z80.h"
    static volatile int nmi_pending = 0;
    static volatile int running = 0;
    //static volatile const uint8_t *nmi_rom = NULL;
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
            if (1) {
                // test
                qDebug()<<"Enabling external ROM";
                set_enable_external_rom(1);
            }
        }
        if (running) {
            execute();
        } else {
            usleep(10000);
        }
    }

    void retn_called_hook()
    {
        // Upon retn, restore ROM.
        printf("RETN called, restoring stock ROM\n");
        set_enable_external_rom(0);
        save_sna("snadump.sna");
    }

    void trigger_nmi()
    {
       // nmi_rom = rom;
        nmi_pending = 1;
    }

    void stop_spectrum()
    {
        running = 0;
    }

    void reset_spectrum() {
        //set_enable_external_rom(0);
        do_reset();
        running = 1;
    }

    extern void toggle_audio();

    extern UCHAR external_rom_read(USHORT address)
    {
        return InterfaceZ::get()->romread(address);
    }
    extern void external_rom_write(USHORT address, UCHAR value)
    {
        InterfaceZ::get()->romwrite(address, value);
    }


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
        //log_audio(startpos);
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


UCHAR interfacez__ioread(void*user,USHORT address)
{
    return static_cast<InterfaceZ*>(user)->ioread(address);
}

void interfacez__iowrite(void*user,USHORT address, UCHAR value)
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
    int r  = register_expansion_port(0x0001,
                                     0x0001,
                                     &interfacez__ioread,
                                     &interfacez__iowrite,
                                     this
                                    );
    if (r<0)
        return -1;

   
    m_fpgasocket = new QTcpServer();

    connect(m_fpgasocket, &QTcpServer::newConnection, this, &InterfaceZ::newConnection);

    if (!m_fpgasocket->listen(QHostAddress::Any, 8007)) { // QTcpSocket::ReuseAddressHint
        qDebug()<<"Cannot listen";
        return -1;
    }

    m_sna_rom_size = -1;
    m_rom = 0;
    m_ram = 0;
    m_gpiostate = 0xFFFFFFFFFFFFFFFF;
    return r;
}

void InterfaceZ::newConnection()
{
    QTcpSocket *s = m_fpgasocket->nextPendingConnection();
    addConnection(s);
}

void InterfaceZ::addConnection(QAbstractSocket *s)
{
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
                       NULL,
                       c);
    m_clients.push_back(c);
}

void InterfaceZ::setCommsSocket(int sock)
{

    QAbstractSocket *s = new QAbstractSocket(QAbstractSocket::UnknownSocketType,this);
    s->setSocketDescriptor(sock);
    addConnection(s);
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

UCHAR InterfaceZ::ioread(USHORT address)
{
    uint8_t val = 0xff;

    //if ((address & 0x8003)==0x8001) {
    //    return 0x00;
    //}
    //printf("IO read %04x\n", address);
    switch (address & 0xFF) {
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
        //printf("return RAM[0x%06x] = 0x%02x\n", extramptr, extram[extramptr]);
        val = extram[extramptr++];
        break;

    case FPGA_PORT_RAM_ADDR_0:
        val = extramptr & 0xFF;
        break;

    case FPGA_PORT_RAM_ADDR_1:
        val = (extramptr>>8) & 0xFF;
        break;

    case FPGA_PORT_RAM_ADDR_2:
        val = (extramptr>>16) & 0xFF;
        break;
    case 0x1F: // Joy port
        val = 0x00;
        break;
    }

//    printf("port read %04x = %02x\n", address, val);

    return val;
}



void InterfaceZ::iowrite(USHORT address, UCHAR value)
{


    //if ((address & 0x8003)==0x8001) {
    //printf("port write %04x = %02x\n", address, value);
    //   return;
    //}

   

    switch (address & 0xFF) {
    case FPGA_PORT_SCRATCH0:
        if (value=='\n') {
            printf("DEBUG: %s\n", m_debug.toLatin1().constData());
            m_debug.clear();
        } else {
            m_debug.append(char(value));
        }
        break;



    case FPGA_PORT_CMD_FIFO_DATA: // Cmd fifo
        //printf("CMD FIFO write: 0x%04x 0x%02x\n", address, value);
        if (m_cmdfifo.size()<32) {
            m_cmdfifo.push_back(value);
            cmdFifoWriteEvent();
        }
        break;
    case FPGA_PORT_RAM_ADDR_0:
        // Address LSB
        extramptr &= 0xFFFF00;
        extramptr |= value;
        //printf("EXT ram pointer (0): %06x\n", extramptr);
        break;

    case FPGA_PORT_RAM_ADDR_1:
        // Address hSB
        extramptr &= 0xFF00FF;
        extramptr |= ((uint32_t)value)<<8;
        //printf("EXT ram pointer (1): %06x\n", extramptr);
        break;

    case FPGA_PORT_RAM_ADDR_2:
        // Address MSB
        extramptr &= 0x00FFFF;
        // We only use 2 banks.
        value &= 0x3;

        extramptr |= ((uint32_t)value)<<16;
        //printf("EXT ram pointer (2): %06x (%02x)\n", extramptr, value);
        break;

    case FPGA_PORT_RAM_DATA:
        //printf("RAM[0x%06x] = 0x%02x\n", extramptr, value);
        extram[extramptr++] = value;
        if (extramptr>=sizeof(extram)) {
            extramptr=0;
        }
        break;
    default:
        printf("Unknown IO port accessed: %04x\n", address);
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
            *(extram+i) = *(p++);
    } else {
        printf("Erorr loading custom ROM from %s\n", name);

        return;
    }

    set_enable_external_rom(1);

    customromloaded = true;
}

void InterfaceZ::hdlcDataReady(Client *c, const uint8_t *data, unsigned datalen)
{
    uint8_t cmd = data[0];
    uint8_t *txbuf_complete = (uint8_t*)malloc(datalen+ 8);
    txbuf_complete[0] = cmd;

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
    case FPGA_SPI_CMD_READ_CAP:
        fpgaCommandReadCapture(data, datalen, txbuf);
        break;
    case FPGA_SPI_CMD_WRITE_CAP:
        fpgaCommandWriteCapture(data, datalen, txbuf);
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

    if (datalen<2)
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


    txbuf[1] = status;
}

void InterfaceZ::fpgaSetFlags(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);

    if (datalen<3)
        throw DataShortException();

    uint16_t old_flags = fpga_flags;

    //printf("Set Flags %02x %02x %02x\n", data[0], data[1], data[2]);

    fpga_flags = ((uint16_t)data[0]) | (data[2]<<8);

    // Triggers
    //printf("Triggers: %02x\n", data[1]);
    
    if (data[1] & FPGA_FLAG_TRIG_RESOURCEFIFO_RESET) {
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCEROMONRETN) {
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCEROMCS_ON) {
        set_enable_external_rom(1);
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCEROMCS_OFF) {
        set_enable_external_rom(0);
    }
    if (data[1] & FPGA_FLAG_TRIG_INTACK) {
    }
    if (data[1] & FPGA_FLAG_TRIG_CMDFIFO_RESET) {
        m_cmdfifo.clear();
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCENMI_ON) {
        trigger_nmi();
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCENMI_OFF) {
    }

    if ( (old_flags & FPGA_FLAG_RSTSPECT)
        && !(fpga_flags & FPGA_FLAG_RSTSPECT) ) {
        reset_spectrum();
    }
    if (fpga_flags & FPGA_FLAG_RSTSPECT) {
        stop_spectrum();
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


    // Skip one byte.
    memcpy( &txbuf[4], &extram[offset], datalen);
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
#if 0
    do {
        printf("Data mem write %08x: [", offset);

        for (int i=0;i<datalen;i++) {
            printf(" %02x", data[3+i]);
        }
        printf(" ]\n");
    } while (0);
#endif
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
    if (datalen<3)
        throw DataShortException();

    bool empty = m_cmdfifo.empty();
    if (empty) {
        txbuf[1] = 0xFF;
    } else {
        txbuf[1] = 0x00;
        uint8_t v = m_cmdfifo.front();
      //  printf("Read CMD fifo: %02x\n", v);
        txbuf[2] = v;
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


#define MEMLAYOUT_ROM0_BASEADDRESS (0x000000)
#define MEMLAYOUT_ROM0_SIZE        (0x002000)
#define MEMLAYOUT_ROM1_BASEADDRESS (0x002000)
#define MEMLAYOUT_ROM1_SIZE        (0x002000)
#define MEMLAYOUT_ROM2_BASEADDRESS (0x004000)
#define MEMLAYOUT_ROM2_SIZE        (0x004000)

#define MEMLAYOUT_RAM_BASEADDRESS(x) (0x010000 + ((x)+0x2000))
#define MEMLAYOUT_RAM_SIZE(x) (0x1FFF)


#define NMI_ROM_BASEADDRESS MEMLAYOUT_ROM0_BASEADDRESS
#define NMI_ROM_SIZE MEMLAYOUT_ROM0_SIZE

UCHAR InterfaceZ::romread(USHORT address)
{
    uint8_t value = 0;
    switch (m_rom) {
    case 0:
        if (address<0x2000) {
            value = extram[MEMLAYOUT_ROM0_BASEADDRESS+address];
        } else {
            value = extram[MEMLAYOUT_RAM_BASEADDRESS(m_ram)+address];
         //   printf("ROM READ %04x: %02x\n", address, value);
        }
        break;
    case 1:
        if (address<0x2000) {
            value = extram[MEMLAYOUT_ROM1_BASEADDRESS+address];
        } else {
            value = extram[MEMLAYOUT_RAM_BASEADDRESS(m_ram)+address];
         //   printf("ROM READ %04x: %02x\n", address, value);
        }
        break;
    case 2:
        value = extram[MEMLAYOUT_ROM2_BASEADDRESS+address];
        break;
    default:
        break;
    }
    return value;
}

void InterfaceZ::romwrite(USHORT address, UCHAR value)
{
    //printf("ROM WRITE %04x: %02x\n", address, value);
    switch (m_rom) {
    case 0: /* Fall-through */
    case 1:
        extram[MEMLAYOUT_RAM_BASEADDRESS(m_ram)+address] = value;
        break;
    default:
        break;
    }
}

void InterfaceZ::sendGPIOupdate()
{
    for (auto c: m_clients) {
        c->sendGPIOupdate(m_gpiostate);
    }

}

void InterfaceZ::Client::sendGPIOupdate(uint64_t v)
{
    qDebug()<<"Sending update GPIO";
    hdlc_encoder__begin(&m_hdlc_encoder);
    uint8_t cmd[9]= { 0x02,
    (uint8_t)((v>>56)&0xff),
    (uint8_t)((v>>48)&0xff),
    (uint8_t)((v>>40)&0xff),
    (uint8_t)((v>>32)&0xff),
    (uint8_t)((v>>24)&0xff),
    (uint8_t)((v>>16)&0xff),
    (uint8_t)((v>>8)&0xff),
    (uint8_t)((v>>0)&0xff),
    };
    {
        unsigned int i;
        for (i=0;i<sizeof(cmd);i++) {
            printf("0x%02x ", cmd[i]);
        }
        printf("\n");
    }
    hdlc_encoder__write(&m_hdlc_encoder, &cmd, sizeof(cmd));
    hdlc_encoder__end(&m_hdlc_encoder);
}

void InterfaceZ::linkGPIO(QPushButton *button, uint32_t gpionum)
{
    connect(button, &QPushButton::pressed, this, [this,gpionum](){ m_gpiostate &= ~(1ULL<<gpionum);
            qDebug()<<"Clear"<<gpionum;
            sendGPIOupdate(); } );
    connect(button, &QPushButton::released, this, [this,gpionum](){ m_gpiostate |= (1ULL<<gpionum);
            qDebug()<<"Set"<<gpionum;
            sendGPIOupdate(); } );
}


void InterfaceZ::fpgaCommandWriteCapture(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    const uint8_t *dptr;

    if (datalen<2)
        return;

    uint16_t address = (uint16_t)data[1] | ((uint16_t)data[0]<<8);
    printf("Capture address %08x\n", address);

    datalen-=2;

    dptr = &data[2];

    while (datalen--) {
        if (address & (1<<13)) {
        } else {
            // register access
            printf("Off %08x\n", address&31);
            m_capture_wr_regs.raw[address & 31] = *dptr++;
            captureRegsWritten();
        }
        address++;
    }
}

void InterfaceZ::fpgaCommandReadCapture(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    if (datalen<3)
        return;

    uint16_t address = (uint16_t)data[1] | ((uint16_t)data[0]<<8);
    printf("Capture address %08x\n", address);
    datalen-=3;
    txbuf+=3; // Move past address and dummy

    if (address & (1<<13)) {
        printf("Capture RAM access, RAM %d offset %d ",
               address&(1<<12)?1:0, address&4095);
    }
    while (datalen--) {
        if (address & (1<<13)) {
            if (address & (1<<12)) {
                // Non-trig
                *txbuf++ = m_capture_buffer_nontrig[ address & 4095];
            } else {
                *txbuf++ = m_capture_buffer_trig[ address & 4095];
            }
        } else {
            *txbuf++ = m_capture_rd_regs.raw[address& 15];
        }
        address++;
    }
}

void InterfaceZ::simulateCapture()
{
    uint32_t *trig = (uint32_t*)&m_capture_buffer_trig[0];
    uint32_t *nontrig = (uint32_t*)&m_capture_buffer_nontrig[0];
    int i;
    for (i=0;i<1024;i++) {
        *nontrig++ = i & 0xff;
        *trig++ = 0;
    }
}

void InterfaceZ::captureRegsWritten()
{
    printf("Capture regs written\n");
    printf(" * Mask : %08x\n", m_capture_wr_regs.mask);
    printf(" * Val  : %08x\n", m_capture_wr_regs.val);
    printf(" * Edge : %08x\n", m_capture_wr_regs.edge);
    printf(" * Ctrl : %08x\n", m_capture_wr_regs.control);

    if (m_capture_wr_regs.control & 0x80000000) {
        qDebug()<<"********* Capture started *********";
        m_capture_wr_regs.control &= ~0x80000000;
        // Simulate read
        simulateCapture();
        m_capture_rd_regs.status = (1<<0); // Counter zero
        m_capture_rd_regs.counter = 0;
        m_capture_rd_regs.trigger_address = 0x1F0;
        m_capture_rd_regs.control;

    }
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

void log_init(const char *filename)
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


