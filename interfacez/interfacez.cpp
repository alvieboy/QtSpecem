#include "interfacez.h"
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
#include "vcdlog.h"
#include <QtGlobal>

/* FPGA IO ports */

#define FPGA_PORT_SCRATCH0	(0x23)
#define FPGA_PORT_MISCCTRL	(0x27)
#define FPGA_PORT_CMD_FIFO_STATUS (0x2B)
#define FPGA_PORT_CMD_FIFO_DATA (0x67)
#define FPGA_PORT_RESOURCE_FIFO_STATUS (0x2F)
#define FPGA_PORT_RESOURCE_FIFO_DATA (0x33)
#define FPGA_PORT_RAM_ADDR_0 (0x37)
#define FPGA_PORT_RAM_ADDR_1 (0x3B)
#define FPGA_PORT_RAM_ADDR_2 (0x3F)
#define FPGA_PORT_RAM_DATA (0x63)
#define FPGA_PORT_NMIREASON (0x6F)
#define FPGA_PORT_MEMSEL  (0x6B)
#define FPGA_PORT_PSEUDOAUDIO (0x73)
#define FPGA_PORT_DEBUG (0x7F)


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
static int interfacez_debug_level = 1;

void interfacez_debug(const char *fmt, ...)
{
    if (interfacez_debug_level>0) {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\r\n");
    }
}

void interfacez_debug_buffer(const uint8_t *buf, unsigned len, const char *fmt, ...)
{
    char line[8192];
    char *endl = &line[0];
    int i;

    if (interfacez_debug_level>0) {
        va_list ap;
        va_start(ap, fmt);
        endl += vsprintf(line, fmt, ap);
        va_end(ap);
        endl = stpcpy(endl, ": [");
        for (i=0;i<len;i++) {
            endl += sprintf(endl, " %02x", buf[i]);
        }
        endl = stpcpy(endl, "]\r\n");
        printf("%s", line);
    }
}

extern "C" {
#include "z80core/iglobal.h"
#include "z80core/z80.h"

#undef IX
#undef IY
#undef SP
#undef IFF1
#undef IFF2
#undef clock_ticks

    int scr_save(FILE * fp);
    int next_save_sequence = 0;

    static int hook_external_rom_active = 0;
    static int main_external_rom_active = 0;
    static volatile int pending_screenshot = 0;

    static void update_rom_active() {
        int prev = get_enable_external_rom();
        int newa = hook_external_rom_active | main_external_rom_active;
        interfacez_debug("Switch ROM enabled, old=%d %d %d", prev, hook_external_rom_active, main_external_rom_active);
        if (prev!=newa) {
            set_enable_external_rom(newa);
        }
    }

    void request_screenshot()
    {
        pending_screenshot = 1;
    }

    void do_screenshot(QImage &i)
    {
        char filename[128];
        do {
            sprintf(filename,"screenshot-%04d.png", next_save_sequence);
            QFile qf(filename);
#if QT_VERSION >= QT_VERSION_CHECK(5,11,0)
            if (!qf.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {
#else
            if (!qf.exists()) {
                qf.open(QIODevice::WriteOnly);
            } else {
#endif
                next_save_sequence++;
                continue;
            }
            if (qf.isOpen()) {
                i.save(&qf, "PNG"); // writes image into ba in PNG format
                qf.close();
                //scr_save(f);
                next_save_sequence++;
                interfacez_debug("Saved screenshot %s",filename);
            } else {
                interfacez_debug("Cannot save screenshot %s",filename);
            }
            break;
        } while (1);
    }

    void z80_interrupt_callback()
    {
    }

    int external_rom_read_hooked(USHORT address)
    {
        return InterfaceZ::get()->external_rom_read_hooked(address);
    }


    static inline void set_main_external_rom_active(int val)
    {
        main_external_rom_active=val;
        update_rom_active();
    }

    static inline void set_hook_external_rom_active(int val)
    {
        //hook_external_rom_active=val;
        //update_rom_active();
        set_main_external_rom_active(val);
    }
    static inline int get_main_external_rom_active() {
        return main_external_rom_active;
    }

    static volatile int nmi_pending = 0;
    static volatile int running = 0;
    static volatile int forceromonret = 0;
    static volatile int romcs_active_on_nmi = 0;
    static volatile int divmmc_compat = 1;
    //static volatile const uint8_t *nmi_rom = NULL;
    void save_sna(const char * file_name);
    extern void open_sna(const char*);
    extern uint16_t read_DE();

    extern char * ldissbl(USHORT adress);
    FILE  *trace_file = NULL;

    extern unsigned long long get_clock_ticks_since_startup(void);

    void execute_if_running()
    {
        if (nmi_pending) {
            nmi_pending=0;
            romcs_active_on_nmi = get_main_external_rom_active();
            do_nmi_int();
            if (1) {
                // test
                interfacez_debug("Enabling external ROM");
                set_main_external_rom_active(1);
            }
        }
        if (running) {
            execute();
            // Trigger interrupt
            InterfaceZ::get()->raiseInterrupt(FPGA_INTERRUPT_SPECT_OFFSET);
        } else {
            usleep(10000);
        }
    }

    void retn_called_hook()
    {
        // Upon retn, restore ROM.
        interfacez_debug("RETN called, %srestoring stock ROM", romcs_active_on_nmi?"CS was active upon entry, NOT ": "");
        set_main_external_rom_active(romcs_active_on_nmi);
        //save_sna("snadump.sna");
    }

    void ret_called_hook()
    {
        if (forceromonret) {
            interfacez_debug("RET called, restoring stock ROM");
            set_main_external_rom_active(0);
            forceromonret=0;
        }
    }

    void cpu_halted_interrupts_disabled()
    {
        interfacez_debug("CPU halted with interrupts disabled");
        if (trace_file)
            fclose(trace_file);
    }
    void trigger_nmi()
    {
       // nmi_rom = rom;
        nmi_pending = 1;
    }

    void stop_spectrum()
    {
        interfacez_debug("Stopping spectrum");
        running = 0;
    }

    void reset_spectrum() {
        //set_enable_external_rom(0);
        interfacez_debug("Hard resetting spectrum");
        do_reset();
        running = 1;
    }

    static uint8_t prev_ula = 0;
    void writeport_ula(UCHAR value)
    {
        if ((value ^ prev_ula) & (1<<4))  {
            InterfaceZ::get()->micToggle();
        }
        prev_ula = value;
    }

    extern void toggle_audio();
    extern UCHAR get_audio();

    extern UCHAR external_rom_read(USHORT address)
    {
        return InterfaceZ::get()->romread(address);
    }
#if 0
    extern int rom_is_hooked(USHORT address)
    {
        return InterfaceZ::get()->isHooked(address)?1:0;
    }
#endif
    extern void external_rom_write(USHORT address, UCHAR value)
    {
        InterfaceZ::get()->romwrite(address, value);
    }


    void insn_executed(unsigned short addr, unsigned long long ticks)
    {
        InterfaceZ::get()->insn_executed(addr, ticks);
    }


    void insn_prefetch(unsigned short addr, unsigned long long clock,
                       struct Z80vars *vars, union Z80Regs *regs,
                        union Z80IX *ix, union Z80IY *iy
                      ) {
        InterfaceZ::get()->insn_prefetch(addr,clock,vars,regs,ix,iy);
    }

    unsigned long long startpos;

    void audio_start()
    {
        startpos = get_clock_ticks_since_startup();
        interfacez_debug("Start play %lld", startpos);
    }

    void audio_push(unsigned delta)
    {
        startpos += delta;
        //interfacez_debug("Pulse %u\n", delta);
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





UCHAR interfacez__ioread(void*user,USHORT address)
{
    return static_cast<InterfaceZ*>(user)->ioread(address);
}

void interfacez__iowrite(void*user,USHORT address, UCHAR value)
{
    static_cast<InterfaceZ*>(user)->iowrite(address, value);
}

InterfaceZ::InterfaceZ()
{
    unsigned i;
    customromloaded = false;
    for (i=0;i<MAX_ROM_HOOKS;i++) {
        rom_hooks[i].flags = 0;
    }
    m_intline = 0;
    m_interruptenabled = true;
    fpga_flags = 0;
    m_kempston = 0x1F;
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
        interfacez_debug("Cannot listen");
        return -1;
    }

    m_sna_rom_size = -1;
    m_rom = 0;
    m_spectrumrom = 0; // only for 128K
    m_ram = 0;
    m_gpiostate = 0xFFFFFFFFFFFFFFFF;
    m_miscctrl = 0;
    m_micidle = 0;

    m_micidletimer.setSingleShot(false);
    m_micidletimer.start(500);
    return r;
}


void InterfaceZ::micIdleTimerExpired()
{
    if (m_micidle!=255)
        m_micidle++;
}

void InterfaceZ::micToggle()
{
    m_micidle=0;
}

void InterfaceZ::enableTrace(const char *file, bool startimmediately)
{
    interfacez_debug("Setting trace file to '%s', immediate %s",
                     file,
                     startimmediately?"yes":"no");
    m_tracefilename = file;

    if (startimmediately) {
        trace_file = fopen(file, "w");
    }
}

void InterfaceZ::newConnection()
{
    QTcpSocket *s = m_fpgasocket->nextPendingConnection();
    addConnection(s);
}

void InterfaceZ::addConnection(QAbstractSocket *s)
{
    s->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    Client *c = new SocketClient(this, s);

    m_clients.push_back(c);
}

void InterfaceZ::addClient(Client *c)
{
    m_clients.push_back(c);
}

void InterfaceZ::setCommsSocket(int sock)
{
    QAbstractSocket *s = new QAbstractSocket(QAbstractSocket::UnknownSocketType,this);
    s->setSocketDescriptor(sock);
    s->open(QIODevice::ReadWrite);
    addConnection(s);
}



void InterfaceZ::removeClient(Client *c)
{
    int i = m_clients.indexOf(c);
    if (i != -1) {
        m_clients.removeAt(i);
    }
    delete(c);
}

UCHAR InterfaceZ::ioread(USHORT address)
{
    uint8_t val = 0xff;
    unsigned size;
    //if ((address & 0x8003)==0x8001) {
    //    return 0x00;
    //}
    //interfacez_debug("IO read %04x\n", address);
    switch (address & 0xFF) {
    /*case 0x05:
        val = 0x39; //Bg
        break;
      */
    case FPGA_PORT_SCRATCH0:
        interfacez_debug("IO Read scratch: %02x\n", m_scratch0);
        val = m_scratch0;
        break;

    case FPGA_PORT_MISCCTRL:
        val = m_miscctrl;
        break;
    case FPGA_PORT_CMD_FIFO_STATUS: // Cmd fifo status
        m_cmdfifomutex.lock();
        size = m_cmdfifo.size();
        m_cmdfifomutex.unlock();
        if (size>=32) {
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
        //interfacez_debug("return RAM[0x%06x] = 0x%02x\n", extramptr, extram[extramptr]);
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
        val = m_kempston;
        break;
    case FPGA_PORT_PSEUDOAUDIO:
        if (fpga_flags & 1<<10) {
            if (get_audio()) {
                val = 0x0D;
            } else {
                val = 0x07;
            }
        } else {
            val = 0;
        }
        break;
    }

//    interfacez_debug("port read %04x = %02x\n", address, val);

    return val;
}



void InterfaceZ::iowrite(USHORT address, UCHAR value)
{


    //if ((address & 0x8003)==0x8001) {
    //interfacez_debug("port write %04x = %02x\n", address, value);
    //   return;
    //}

   

    switch (address & 0xFF) {

    case FPGA_PORT_SCRATCH0:
        interfacez_debug("IOWRITE scratch %02x", value);
        m_scratch0 = value;
        break;
    case FPGA_PORT_DEBUG:
        if (value=='\n') {
            interfacez_debug("DEBUG: %s", m_debug.toLatin1().constData());
            m_debug.clear();
        } else {
            m_debug.append(char(value));
        }
        break;



    case FPGA_PORT_CMD_FIFO_DATA: // Cmd fifo
        interfacez_debug("CMD FIFO write: 0x%04x 0x%02x", address, value);
        m_cmdfifomutex.lock();
        if (m_cmdfifo.size()<32) {
            m_cmdfifo.push_back(value);
            m_cmdfifomutex.unlock();
            cmdFifoWriteEvent();
        } else {
            m_cmdfifomutex.unlock();
        }
        break;
    case FPGA_PORT_RAM_ADDR_0:
        // Address LSB
        extramptr &= 0xFFFF00;
        extramptr |= value;
        //interfacez_debug("EXT ram pointer (0): %06x", extramptr);
        break;

    case FPGA_PORT_RAM_ADDR_1:
        // Address hSB
        extramptr &= 0xFF00FF;
        extramptr |= ((uint32_t)value)<<8;
        //interfacez_debug("EXT ram pointer (1): %06x", extramptr);
        break;

    case FPGA_PORT_RAM_ADDR_2:
        // Address MSB
        extramptr &= 0x00FFFF;
        // We only use 2 banks.
        value &= 0x3;

        extramptr |= ((uint32_t)value)<<16;
        //interfacez_debug("EXT ram pointer (2): %06x (%02x)", extramptr, value);
        break;

    case FPGA_PORT_RAM_DATA:
        //interfacez_debug("RAM[0x%06x] = 0x%02x", extramptr, value);
        extram[extramptr++] = value;
        if (extramptr>=sizeof(extram)) {
            extramptr=0;
        }
        break;

    case FPGA_PORT_NMIREASON:
        if (value & 1)
            forceromonret = 1;
        if (value & 2) {
            if (get_main_external_rom_active()) {
                set_main_external_rom_active(0);
            }
        } else {
            set_main_external_rom_active(1);
        }
        break;
    case FPGA_PORT_MEMSEL:
        m_ram = value & 0x7;
        printf("New RAM selected: %d\n", m_ram);
        break;

    default:
        interfacez_debug("Unknown IO port accessed: %04x", address);
    }
}


static int do_trace=0;
void rom_access_hook(USHORT address, UCHAR data)
{
    if (do_trace) {
        interfacez_debug("ROM: %04x: %02x", address, data);
    }
}
void enable_trace()
{
    do_trace=1;
}

void InterfaceZ::onNMI()
{
    interfacez_debug(" ***** NMI ***** (custom: %s)", customromloaded?"yes":"no");
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
    interfacez_debug("Loading custom ROM from %s", name);
    if(file.open(QIODevice::ReadOnly)){
        data=file.readAll();
        file.close();
        p=data;
        for (int i=0; i < 16384 ; i++)
            *(extram+i) = *(p++);
    } else {
        interfacez_debug("Erorr loading custom ROM from %s", name);

        return;
    }

    set_main_external_rom_active(1);

    customromloaded = true;
}

void InterfaceZ::transceive(Client *c, const uint8_t *data, uint8_t *txbuf, unsigned datalen)
{
    //printf("IZ transceive %p %p %d\n", data, txbuf,datalen);
    uint8_t cmd = data[0];

    //uint8_t *txbuf_complete = (uint8_t*)malloc(datalen+ 8);
    //txbuf_complete[0] = cmd;

    //interfacez_debug("CMD: 0x%02x len %d", cmd, datalen);

    if (interfacez_debug_level>6) {
        do{
            printf("[Request] ");
            unsigned i;
            for (i=0;i<datalen;i++) {
                printf(" %02x",data[i]);
            }
            printf("\n");
        } while (0);
    }

    data++;
    datalen--;

    //uint8_t *txdatabuf = &txbuf_complete[1]; // Skip command location.
    try {
        switch(cmd) {
        case FPGA_CMD_READ_STATUS:
            fpgaReadStatus(data, datalen, txbuf);
            break;
        case FPGA_CMD_READ_VIDEO_MEM:
            fpgaReadVideoMem(data, datalen, txbuf);
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
        case FPGA_SPI_CMD_READ_CTRL:
            fpgaCommandReadControl(data, datalen, txbuf);
            break;
        case FPGA_SPI_CMD_WRITE_CTRL:
            fpgaCommandWriteControl(data, datalen, txbuf);
            break;
        case FPGA_SPI_CMD_INTSTATUS:
            fpgaCommandReadIntStatus(data, datalen, txbuf);
            break;
        case FPGA_SPI_CMD_INTCLEAR:
            fpgaCommandWriteIntClear(data, datalen, txbuf);
            break;
        case FPGA_SPI_CMD_SET_ROMRAM:
            fpgaCommandSetRomRam(data, datalen, txbuf);
            break;
        case FPGA_CMD_WRITE_MISCCTRL:
            fpgaCommandWriteMiscCtrl(data, datalen, txbuf);
            break;
        case FPGA_CMD_READ_MIC_IDLE:
            fpgaCommandReadMicIdle(data, datalen, txbuf);
            break;
        case FPGA_CMD_FRAME_SYNC:
            break;
        default:
            fprintf(stderr,"Unknown SPI command 0x%02x\n", cmd);
        }
    } catch (std::exception &e) {
        fprintf(stderr,"Cannot parse SPI block: %s", e.what());
    }

    if (interfacez_debug_level>6) {
        do{
            printf("[Reply] ");
            unsigned i;
            for (i=0;i<datalen+1;i++) {
                printf(" %02x",txbuf[i]);
            }
            printf("\n");
        } while (0);
    }

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



#define FPGA_STATUS_RESFIFO_FULL   (1<<0)
#define FPGA_STATUS_RESFIFO_QFULL  (1<<1)
#define FPGA_STATUS_RESFIFO_HFULL  (1<<2)
#define FPGA_STATUS_RESFIFO_QQQFULL  (1<<3)
//#define FPGA_STATUS_CMDFIFO_EMPTY  (1<<5)

#define RESFIFO_SIZE 1024

void InterfaceZ::fpgaReadStatus(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(data);

    if (datalen<2)
        throw DataShortException();

    uint8_t status = 0;
/*    if (m_cmdfifo.empty()) {
        status |= FPGA_STATUS_CMDFIFO_EMPTY;
    }
  */
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

    m_cmdfifomutex.lock();
    unsigned cmdfifosize = m_cmdfifo.size();
    m_cmdfifomutex.unlock();

    if (cmdfifosize>4)
        cmdfifosize = 4;

    status |= (cmdfifosize<<4); //4,5,6 bit.

    //interfacez_debug("*** returning status %02x", status);
    txbuf[1] = status;
}

extern UCHAR *mem;
#define SPECTRUM_FRAME_SIZE (32*(192+24))

void InterfaceZ::fpgaReadVideoMem(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(data);

    if (datalen<SPECTRUM_FRAME_SIZE+1)
        throw DataShortException();

    memcpy(&txbuf[3], &mem[0x4000], SPECTRUM_FRAME_SIZE);
}

void InterfaceZ::fpgaSetFlags(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);

    if (datalen<3)
        throw DataShortException();

    uint16_t old_flags = fpga_flags;

    interfacez_debug("Set Flags %02x %02x %02x", data[0], data[1], data[2]);

    fpga_flags = ((uint16_t)data[0]) | (data[2]<<8);

    // Triggers
    //interfacez_debug("Triggers: %02x", data[1]);
    
    if (data[1] & FPGA_FLAG_TRIG_RESOURCEFIFO_RESET) {
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCEROMONRETN) {
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCEROMCS_ON) {
        set_main_external_rom_active(1);
    }
    if (data[1] & FPGA_FLAG_TRIG_FORCEROMCS_OFF) {
        set_main_external_rom_active(0);
    }
    if (data[1] & FPGA_FLAG_TRIG_INTACK) {
    }
    if (data[1] & FPGA_FLAG_TRIG_CMDFIFO_RESET) {
        m_cmdfifomutex.lock();
        m_cmdfifo.clear();
        m_cmdfifomutex.unlock();

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
        interfacez_debug("Attempt to read outside extram, offset 0x%08x len %d", offset, datalen);
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
        interfacez_debug("Attempt to read outside extram, offset 0x%08x len %d", offset, datalen);
        return;
    }
#if 0
    do {
        interfacez_debug("Data mem write %08x: [", offset);

        for (int i=0;i<datalen;i++) {
            interfacez_debug(" %02x", data[3+i]);
        }
        interfacez_debug(" ]");
    } while (0);
#endif
    memcpy( &extram[offset], data, datalen);

}
void InterfaceZ::fpgaWriteResFifo(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    //interfacez_debug("Resource FIFO write: [");
    while (datalen--) {
      //  interfacez_debug(" %02x", *data);
        m_resourcefifo.push_back(*data++);
    }
//    interfacez_debug(" ]");
}

void InterfaceZ::fpgaWriteTapFifo(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);

    interfacez_debug_buffer(data, datalen, "TAP FIFO write");

    while (datalen--) {
        m_player.handleStreamData(*data);
        data++;
    }
}

void InterfaceZ::fpgaWriteTapFifoCmd(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    interfacez_debug("TAP FIFO write (cmd): [");
    while (datalen--) {
        m_player.handleStreamData(*data | 0x100);
        interfacez_debug(" %02x", *data);
        data++;
    }
    interfacez_debug(" ]");
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

static const char *config1_names[] = {
    "KBD",
    "JOY",
    "MOUSE",
    "AY",
    "AY_READ",
    "DIVMMC_COMPAT"
};

void InterfaceZ::fpgaSetRegs32(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    uint32_t regdata;
    uint8_t regnum;
    data = extractu8(data, datalen, regnum);
    data = extractbe32(data, datalen, regdata);
    uint32_t olddata = regs[regnum];
    if (regnum<32) {
        regs[regnum] = regdata;
    }
    // Propagate flags
    if (regnum==2) {
        char tempstr[128];
        char *p = tempstr;
        int i;

        *p = 0;
        for (i=0;i<6;i++) {
            if (!(olddata&(1<<i))) {
                if (regdata&(1<<i)) {
                    if (*p) {
                        strcat(p," ");
                    }
                    strcat(p, config1_names[i]);
                }
            }
        }
        if (*p) {
            interfacez_debug("CONFIG1: SET %s", p);
        }

        *p = 0;
        for (i=0;i<6;i++) {
            if (olddata&(1<<i)) {
                if (!(regdata&(1<<i))) {
                    if (*p) {
                        strcat(p," ");
                    }
                    strcat(p, config1_names[i]);
                }
            }
        }
        if (*p) {
            interfacez_debug("CONFIG1: CLEAR %s", p);
        }

        divmmc_compat = (regs[2]>>5) & 1;
    }
    if (regnum==5) {
        interfacez_debug("Joystick update: %08x", regdata);
        m_kempston = (regdata>>18) & 0x1F;

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


    uint8_t len = data[0];
    uint8_t *ptr = &txbuf[2];

    datalen-=2;

    interfacez_debug("Request %d from fifo, datalen %d fifo size %d",
                     len,
                     datalen,
                     m_cmdfifo.size());

    m_cmdfifomutex.lock();

    while (len--) {
        if (datalen==0)
            break;
        if (m_cmdfifo.empty())
            break;

        uint8_t v = m_cmdfifo.front();
        //interfacez_debug("Data: %02x", v);
        m_cmdfifo.pop_front();
        *ptr++ = v;
        datalen--;
    }
    m_cmdfifomutex.unlock();

    interfacez_debug("End of request, fifo len %d",m_cmdfifo.size());
    if (m_cmdfifo.size()==0)
        lowerInterrupt(0);
}



void InterfaceZ::cmdFifoWriteEvent()
{
    raiseInterrupt(0);
}


void InterfaceZ::raiseInterrupt(uint8_t index)
{
    // Activate interrupt line
    m_intmutex.lock();
    bool wasenabled = m_interruptenabled;
    m_intline |= (1<<index);
    m_interruptenabled = false;
    m_intmutex.unlock();


    if (wasenabled) {
        for (auto c: m_clients) {
            c->gpioEvent(PIN_NUM_CMD_INTERRUPT);
        }
    }
}

void InterfaceZ::lowerInterrupt(uint8_t index)
{
    m_intmutex.lock();
    m_intline &= ~(1<<index);
    m_intmutex.unlock();
}

#define MEMLAYOUT_ROM0_BASEADDRESS (0x000000)
#define MEMLAYOUT_ROM0_SIZE        (0x002000)
#define MEMLAYOUT_ROM1_BASEADDRESS (0x002000)
#define MEMLAYOUT_ROM1_SIZE        (0x002000)
#define MEMLAYOUT_ROM2_BASEADDRESS (0x004000)
#define MEMLAYOUT_ROM2_SIZE        (0x004000)

#define MEMLAYOUT_RAM_BASEADDRESS(x) (0x010000 + ((x)*0x2000))
#define MEMLAYOUT_RAM_MASK(addr) ( (addr) & 0x1FFF )


#define NMI_ROM_BASEADDRESS MEMLAYOUT_ROM0_BASEADDRESS
#define NMI_ROM_SIZE MEMLAYOUT_ROM0_SIZE

UCHAR InterfaceZ::romread(USHORT address)
{
    uint8_t value = 0;
    unsigned extram_address;

    switch (m_rom) {
    case 0:
        if (address<0x2000) {
            value = extram[MEMLAYOUT_ROM0_BASEADDRESS+address];
        } else {
            extram_address = MEMLAYOUT_RAM_BASEADDRESS(m_ram) + MEMLAYOUT_RAM_MASK(address & 0x1fff);
            value = extram[extram_address];
#if 0
            interfacez_debug("ROM READ [ram %d] %04x: %02x [%08x base %08x]",
                             m_ram,
                             address,
                             value,
                             extram_address,
                             MEMLAYOUT_RAM_BASEADDRESS(m_ram));
#endif
        }
        break;
    case 1:
        if (address<0x2000) {
            value = extram[MEMLAYOUT_ROM1_BASEADDRESS+address];
        } else {
            extram_address = MEMLAYOUT_RAM_BASEADDRESS(m_ram) + MEMLAYOUT_RAM_MASK(address);
            value = extram[extram_address];
#if 0
            interfacez_debug("ROM READ [ram %d] %04x: %02x [%08x]", m_ram, address, value, extram_address);
#endif
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
    unsigned extram_address = MEMLAYOUT_RAM_BASEADDRESS(m_ram)+MEMLAYOUT_RAM_MASK(address);
#if 0
    interfacez_debug("ROM WRITE [ram %d] %04x: %02x [%08x]", m_ram, address, value, extram_address);
#endif
    switch (m_rom) {
    case 0: /* Fall-through */
    case 1:
        extram[extram_address] = value;
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

void InterfaceZ::sendConnectUSB(const char *id)
{
    for (auto c: m_clients) {
        c->connectUSB(id);
    }
}


void InterfaceZ::linkGPIO(QPushButton *button, uint32_t gpionum)
{
    connect(button, &QPushButton::pressed, this, [this,gpionum](){ m_gpiostate &= ~(1ULL<<gpionum);
            //qDebug()<<"Clear"<<gpionum;
            sendGPIOupdate(); } );
    connect(button, &QPushButton::released, this, [this,gpionum](){ m_gpiostate |= (1ULL<<gpionum);
            //qDebug()<<"Set"<<gpionum;
            sendGPIOupdate(); } );
}


void InterfaceZ::fpgaCommandWriteCapture(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    Q_UNUSED(txbuf);
    const uint8_t *dptr;

    if (datalen<2)
        return;

    uint16_t address = (uint16_t)data[1] | ((uint16_t)data[0]<<8);
    interfacez_debug("Capture address %08x", address);

    datalen-=2;

    dptr = &data[2];

    while (datalen--) {
        if (address & (1<<13)) {
        } else {
            // register access
            interfacez_debug("Off %08x", address&31);
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
    interfacez_debug("Capture address %08x", address);
    datalen-=3;
    txbuf+=3; // Move past address and dummy

    if (address & (1<<13)) {
        interfacez_debug("Capture RAM access, RAM %d offset %d ",
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

void InterfaceZ::fpgaCommandReadControl(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    uint16_t address = (uint16_t)data[1] | ((uint16_t)data[0]<<8);
    address &= (1<<12)-1; // Only 12 bits for address
}

void InterfaceZ::fpgaCommandWriteControl(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    uint16_t address = (uint16_t)data[1] | ((uint16_t)data[0]<<8);
    address &= (1<<12)-1; // Only 12 bits for address
    data+=2;
    datalen-=2;
    interfacez_debug("CONTROL write");
    while (datalen--) {
        //when "1000000" | "1000100" | "1001000" | "1001100" => -- Hook low
        if ((address & 0x40) == 0x40) {
            // ROM Hook control
            unsigned hookno = (address >> 2) & (MAX_ROM_HOOKS-1);
            if (hookno>MAX_ROM_HOOKS) {
                interfacez_debug("MAX hooks exceeded");
            }
            hookno &= (MAX_ROM_HOOKS-1);

            interfacez_debug("HOOK write %04x", address);

            switch (address&0x03) {
            case 0x00:
                // Low
                rom_hooks[hookno].base &= 0xFF00;
                rom_hooks[hookno].base |= *data;
                break;
            case 0x01:
                // High
                rom_hooks[hookno].base &= 0x00FF;
                rom_hooks[hookno].base |= (((uint16_t)*data)<<8) & 0x3FFF;
                break;
            case 0x02:
                // Len
                rom_hooks[hookno].masklen = (*data) & 0x03;
                break;
            case 0x03:
                // flags
                rom_hooks[hookno].flags = *data;
                if (rom_hooks[hookno].flags) {
                    interfacez_debug("HOOK %d activated, address %04x masklen %d", hookno,
                                     rom_hooks[hookno].base,
                                     rom_hooks[hookno].masklen);
                }
                break;
            }
        }
        data++;
        address++;
    }
}

void InterfaceZ::fpgaCommandReadIntStatus(const uint8_t *data, int datalen, uint8_t *txbuf)
{
  //  printf("Reading int status 0x%0x\n", m_intline);
    txbuf[1] = m_intline;
}

void InterfaceZ::fpgaCommandReadMicIdle(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    txbuf[1] = m_micidle;
}

void InterfaceZ::fpgaCommandWriteIntClear(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    m_intmutex.lock();
    m_intline &= ~data[0];
    m_interruptenabled = true;
    bool propagate = m_intline !=0 ;
    m_intmutex.unlock();

    // This cannot block or we can lock SPI for good.

    if (propagate) {
        for (auto c: m_clients) {
            c->gpioEvent(PIN_NUM_CMD_INTERRUPT);
        }
    }
}

void InterfaceZ::fpgaCommandSetRomRam(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    uint8_t romram = data[0];
    if (romram & 0x80) {
        // RAM
        m_ram = romram & 0x7;
    } else {
        m_rom = romram & 0x3;
    }
}

void InterfaceZ::fpgaCommandWriteMiscCtrl(const uint8_t *data, int datalen, uint8_t *txbuf)
{
    m_miscctrl = data[0];
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
    interfacez_debug("Capture regs written");
    interfacez_debug(" * Mask : %08x", m_capture_wr_regs.mask);
    interfacez_debug(" * Val  : %08x", m_capture_wr_regs.val);
    interfacez_debug(" * Edge : %08x", m_capture_wr_regs.edge);
    interfacez_debug(" * Ctrl : %08x", m_capture_wr_regs.control);

    if (m_capture_wr_regs.control & 0x80000000) {
        interfacez_debug("********* Capture started *********");
        m_capture_wr_regs.control &= ~0x80000000;
        // Simulate read
        simulateCapture();
        m_capture_rd_regs.status = (1<<0); // Counter zero
        m_capture_rd_regs.counter = 0;
        m_capture_rd_regs.trigger_address = 0x1F0;

    }
}


void InterfaceZ::insn_executed(unsigned short addr, unsigned long long ticks)
{
    //interfacez_debug("%lld\n", ticks);
    if (audio_event_queue.size()) {
        unsigned long long expires = audio_event_queue.front();
        if (expires <= ticks) {
            //interfacez_debug("Audio event %lld %lld\n", expires, ticks);
            audio_event_queue.pop_front();
            toggle_audio();
        }
    }

    /* Check post-hooks and ranged hooks */
    for (int i=0;i<MAX_ROM_HOOKS;i++) {
        if (!(rom_hooks[i].flags & ROM_HOOK_FLAG_ACTIVE))
            continue;

        if ( (rom_hooks[i].flags &ROM_HOOK_FLAG_ROM_MASK)  != ROM_HOOK_FLAG_ROM(m_spectrumrom)) {
            continue;
        }

        if ( (rom_hooks[i].flags & ROM_HOOK_FLAG_RANGED) == ROM_HOOK_FLAG_RANGED )
        {
            // Ranged hook. Always disable at end of instruction
#if 0
            if (hookAddressMatches(addr, rom_hooks[i]) ) {
                set_hook_external_rom_active(0);
            }
#endif
        } else {
            if ((rom_hooks[i].flags & (ROM_HOOK_FLAG_POST))==ROM_HOOK_FLAG_POST) {
                // Post-match hook.
                if (hookAddressMatches( addr, rom_hooks[i] )) {
                    int enable = 0;
                    if (rom_hooks[i].flags & ROM_HOOK_FLAG_SETRESET) {
                        enable = 1;
                    }
                    set_hook_external_rom_active(enable);
                    interfacez_debug("POST hook match address %04x, set %d\n", addr, enable);

                }
            }
        }
    }
}

void InterfaceZ::insn_prefetch(unsigned short addr, unsigned long long clock,
                               struct Z80vars *vars, union Z80Regs *regs,
                               union Z80IX *ix, union Z80IY *iy)
{

    /* Check pre-hooks and ranged hooks */
    for (int i=0;i<MAX_ROM_HOOKS;i++) {
        if (!(rom_hooks[i].flags & ROM_HOOK_FLAG_ACTIVE))
            continue;

        if ( (rom_hooks[i].flags &ROM_HOOK_FLAG_ROM_MASK)  != ROM_HOOK_FLAG_ROM(m_spectrumrom)) {
            continue;
        }

        if ( (rom_hooks[i].flags & ROM_HOOK_FLAG_RANGED) == ROM_HOOK_FLAG_RANGED )
        {
#if 0
            // Ranged hook. Always enable at start of instruction
            if (hookAddressMatches(addr, rom_hooks[i]) ) {
                interfacez_debug("PRE hook range match address %04x", addr);
                set_hook_external_rom_active(1);
            }
#endif

        } else {
            if ((rom_hooks[i].flags & (ROM_HOOK_FLAG_POST))==0) {
                // Pre-match hook.
                if (hookAddressMatches( addr, rom_hooks[i] )) {
                    int enable = 0;
                    if (rom_hooks[i].flags & ROM_HOOK_FLAG_SETRESET) {
                        enable = 1;
                    }
                    set_hook_external_rom_active(enable);
                    interfacez_debug("PRE hook match address %04x, set %d\n", addr, enable);
                }
            }
        }
    }

    if (trace_file==NULL) {
        if (m_traceaddress.size()) {
            for (auto i: m_traceaddress) {
                //interfacez_debug("I %04x %04x", i, addr);
                if (i==addr) {
                    interfacez_debug("Starting trace on '%s', address trigger %04x",
                                     m_tracefilename.c_str(), addr);
                    trace_file = fopen(m_tracefilename.c_str(),"w");
                }
            }
        }
    }

    if (trace_file!=NULL) {
        char line[24];
        char *dis = &ldissbl(addr)[1];
        dis = stpcpy(line, dis);
        while (dis<&line[22]) {
            *dis++=' ';
        }
        *dis = '\0';

        fprintf(trace_file, "0x%04x: %s ; ", addr, line);
        // Write vars
        if (regs) {
            int hooked = external_rom_read_hooked(addr);
            int rom = get_enable_external_rom();
            if (hooked>=0)
                rom = 2;
            fprintf(trace_file, "[%d,%d,%d,%d] AF: %04x BC: %04x DE: %04x HL: %04x SP: %04x IX: %04X IY: %04x IFF1=%d IFF2=%d T=%ld\n",
                    rom,
                    hook_external_rom_active,
                    main_external_rom_active,
                    InterfaceZ::get()->getRam(),
                    regs->x.af,
                    regs->x.bc,
                    regs->x.de,
                    regs->x.hl,
                    vars->SP,
                    ix->IX,
                    iy->IY,
                    vars->IFF1,
                    vars->IFF2,
                    vars->clock_ticks);
        }
    }
}

int InterfaceZ::external_rom_read_hooked(USHORT addr)
{
    for (int i=0;i<MAX_ROM_HOOKS;i++) {
        if (!(rom_hooks[i].flags & ROM_HOOK_FLAG_ACTIVE))
            continue;

        if ( (rom_hooks[i].flags &ROM_HOOK_FLAG_ROM_MASK)  != ROM_HOOK_FLAG_ROM(m_spectrumrom)) {
            continue;
        }

        if ( (rom_hooks[i].flags & ROM_HOOK_FLAG_RANGED) == ROM_HOOK_FLAG_RANGED )
        {
            // Ranged hook. Always enable at start of instruction
            if (hookAddressMatches(addr, rom_hooks[i]) ) {
                return external_rom_read(addr);
            }
        }
    }
    return -1;
}


void InterfaceZ::addTraceAddressMatch(uint16_t address)
{
    interfacez_debug("Add trace start address %04x", address);
    m_traceaddress.push_back(address);
}

void InterfaceZ::screenshot(QImage &i)
{
    do_screenshot(i);
}
