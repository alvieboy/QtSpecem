#ifndef __INTERFACEZ_H__
#define __INTERFACEZ_H__

#include <QTcpServer>
#include <QMap>
#include <QString>
#include <QQueue>
#include <QTimer>
#include <inttypes.h>
#include "expansion/expansion.h"
#include <stdexcept>
#include "Tape.h"
#include <QMutex>

#include "Client.h"
#include "SocketClient.h"
#include "LinkClient.h"

class QTcpSocket;
class QPushButton;

void interfacez_debug(const char *fmt, ...);

#define MAX_ROM_HOOKS 8
#define ROM_HOOK_FLAG_ACTIVE (1<<7)
#define ROM_HOOK_FLAG_SETRESET (1<<6)
#define ROM_HOOK_FLAG_PREPOST (1<<5)
#define ROM_HOOK_FLAG_RANGED  (1<<4)
#define ROM_HOOK_FLAG_ROM(x)  ((x)<<0)
#define ROM_HOOK_FLAG_ROM_MASK (0x1)

/* FPGA commands */

#define FPGA_CMD_READ_STATUS (0xDE)
#define FPGA_CMD_READ_VIDEO_MEM (0xDF)
#define FPGA_CMD_READ_PC (0x40)
#define FPGA_CMD_READ_EXTRAM (0x50)
#define FPGA_CMD_WRITE_EXTRAM (0x51)
#define FPGA_CMD_READ_USB (0x60)
#define FPGA_CMD_WRITE_USB (0x61)
#define FPGA_CMD_WRITE_ROM (0xE1)
#define FPGA_CMD_WRITE_RES_FIFO (0xE3)
#define FPGA_CMD_WRITE_TAP_FIFO (0xE4)
#define FPGA_CMD_WRITE_TAP_FIFO_CMD (0xE6)
#define FPGA_CMD_GET_TAP_FIFO_USAGE (0xE5)
#define FPGA_SPI_CMD_SET_ROMRAM (0xEB)
#define FPGA_CMD_SET_FLAGS (0xEC)
#define FPGA_CMD_SET_REGS32 (0xED)
#define FPGA_CMD_GET_REGS32 (0xEE)
#define FPGA_CMD_READ_CMDFIFO_DATA (0xFB)
#define FPGA_CMD_WRITE_MISCCTRL (0xFC)
#define FPGA_CMD_READID1 (0x9E)
#define FPGA_CMD_READID2 (0x9F)
#define FPGA_SPI_CMD_READ_CAP (0x62)
#define FPGA_SPI_CMD_WRITE_CAP (0x63)
#define FPGA_SPI_CMD_READ_CTRL (0x64)
#define FPGA_SPI_CMD_WRITE_CTRL (0x65)
#define FPGA_SPI_CMD_INTSTATUS (0xA1)
#define FPGA_SPI_CMD_INTCLEAR (0xA0)


#define PIN_NUM_SWITCH 34
#define PIN_NUM_CMD_INTERRUPT 27
#define PIN_NUM_SPECT_INTERRUPT 26
#define PIN_NUM_USB_INTERRUPT 22

typedef union {
    struct {
        uint32_t mask;
        uint32_t val;
        uint32_t edge;
        uint32_t unused;
        uint32_t control;
        uint32_t unused2;
        uint32_t unused3;
        uint32_t unused4;
    };
    uint8_t raw[32];
} capture_wr_regs_t;

typedef union {
    struct {
        uint32_t status;
        uint32_t counter;
        uint32_t trigger_address;
        uint32_t control;
    };
    uint8_t raw[16];
} capture_rd_regs_t;


class InterfaceZ: public QObject
{
    Q_OBJECT
public:

private:
    InterfaceZ();
public:
    // Singleton
    static InterfaceZ *get() {
        if (self==NULL) {
            self = new InterfaceZ();
        }
        return self;
    }
    static InterfaceZ *self;

    int init();
    UCHAR ioread(USHORT address);
    void iowrite(USHORT address, UCHAR value);
    UCHAR romread(USHORT address);
    void romwrite(USHORT address, UCHAR value);
    bool isHooked(USHORT address);

    void loadCustomROM(const char *file);
    void enableTrace(const char *file, bool startimmediatly=false);
    void addTraceAddressMatch(uint16_t address);


    //static void hdlc_writer(void *userdata, const uint8_t c);
    //void hdlc_writer(const uint8_t c);
    //static void hdlc_flusher(void *userdata);
    //void hdlc_flusher(void);
    void setCommsSocket(int socket);
    void addConnection(QAbstractSocket *s);

    void transceive(Client *c, const uint8_t *data, uint8_t *tx,  unsigned datalen);
    void removeClient(Client *c);
    void addClient(Client *c);


    uint8_t getRam() const {return m_ram;}

    void insn_executed(unsigned short addr, unsigned long long ticks);

    void insn_prefetch(unsigned short addr, unsigned long long clock,
                       struct Z80vars *vars, union Z80Regs *regs,
                       union Z80IX *ix, union Z80IY *iy);

    int external_rom_read_hooked(USHORT address);
    void sendConnectUSB(const char *id);

public slots:
    void newConnection();
    //void onSDConnected();
    //void onSDDisconnected();
    //void WiFiScanFinished();
    //void WiFiConnected();
    void onNMI();
    void linkGPIO(QPushButton *button, uint32_t gpionum);
    void raiseInterrupt(uint8_t index);
    void lowerInterrupt(uint8_t index);

protected:
    void fpgaCommandReadID(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaSetFlags(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaReadStatus(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaReadExtRam(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaWriteExtRam(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaWriteResFifo(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaSetRegs32(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaGetRegs32(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaReadCmdFifo(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaWriteTapFifo(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaWriteTapFifoCmd(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaGetTapFifoUsage(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaCommandReadCapture(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaCommandWriteCapture(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaCommandReadControl(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaCommandWriteControl(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaCommandReadIntStatus(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaCommandWriteIntClear(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaCommandSetRomRam(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaCommandWriteMiscCtrl(const uint8_t *data, int datalen, uint8_t *txbuf);
    void cmdFifoWriteEvent();
    void captureRegsWritten();
    void simulateCapture();
    class DataShortException : public std::exception {
    public:
        const char *what() const noexcept { return "Data too short"; }
    };

    static const uint8_t *extractbe16(const uint8_t *source, int &datalen, uint16_t &target){
        if (datalen<2)
            throw DataShortException();
        datalen-=2;
        target = ((uint16_t)*source++)<<8;
        target += (uint16_t)*source++;
        return source;
    }

    static const uint8_t *extractbe24(const uint8_t *source, int &datalen, uint32_t &target)
    {
        if (datalen<3)
            throw DataShortException();
        datalen-=3;
        target = ((uint32_t)*source++)<<16;
        target += ((uint32_t)*source++)<<8;
        target += (uint32_t)*source++;
        return source;
    }

    static const uint8_t *extractbe32(const uint8_t *source, int &datalen, uint32_t &target)
    {
        if (datalen<4)
            throw DataShortException();
        datalen-=4;
        target = ((uint32_t)*source++)<<24;
        target += ((uint32_t)*source++)<<16;
        target += ((uint32_t)*source++)<<8;
        target += (uint32_t)*source++;
        return source;
    }

    static const uint8_t *extractu8(const uint8_t *source, int &datalen, uint8_t &target)
    {
        if (datalen<1)
            throw DataShortException();
        datalen-=1;
        target = *source++;
        return source;
    }
    void sendGPIOupdate();
    void debug(const char *fmt, ...);
signals:
    void tapDataReady();

private:

    struct rom_hook {
        uint16_t base;
        uint8_t len;
        uint8_t flags;
    };

    bool hookAddressMatches(USHORT address, const struct rom_hook &hook) {
        if ((address>=hook.base) &&
            (address<=hook.base + hook.len )) {
            return true;
        }
        return false;
    }

    struct rom_hook rom_hooks[MAX_ROM_HOOKS];

    uint8_t m_rom;
    uint8_t m_spectrumrom;
    uint8_t m_ram;
    uint8_t m_intline;
    bool m_interruptenabled;
    uint64_t m_gpiostate;
    TapePlayer m_player;
    QTcpServer *m_fpgasocket;
    bool customromloaded;
//    uint8_t saverom[16384];

    uint8_t extram[65536*8]; // Only 8 blocks for now.
    uint32_t extramptr;

    uint32_t regs[32];
    uint8_t m_miscctrl;

    uint8_t m_sna[49179];
    unsigned m_sna_size;
    const uint8_t *m_sna_head;
    const uint8_t *m_sna_tail;
    bool m_sna_rom_uploaded;

    uint8_t m_sna_rom[16384];
    int m_sna_rom_size;

    QByteArray m_nmirom;

    QQueue<uint8_t> m_resourcefifo;
    QMutex m_cmdfifomutex;
    QQueue<uint8_t> m_cmdfifo;
    QQueue<uint16_t> m_tapfifo;

    uint8_t m_capture_buffer_nontrig[4096];
    uint8_t m_capture_buffer_trig[4096];

    capture_wr_regs_t m_capture_wr_regs;
    capture_rd_regs_t m_capture_rd_regs;
    uint8_t intline_m;

    QList<Client*> m_clients;
    uint16_t fpga_flags;
    QString m_debug;
    std::vector<uint16_t> m_traceaddress;
    std::string m_tracefilename;
};

#endif
