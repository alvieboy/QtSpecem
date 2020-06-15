#ifndef __INTERFACEZ_H__
#define __INTERFACEZ_H__

#include <QTcpServer>
#include <QMap>
#include <QString>
#include <QQueue>
#include <QTimer>
#include <inttypes.h>
#include "expansion/expansion.h"
#include "hdlc_decoder.h"
#include "hdlc_encoder.h"
#include <stdexcept>
class QTcpSocket;



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
#define FPGA_CMD_SET_FLAGS (0xEC)
#define FPGA_CMD_SET_REGS32 (0xED)
#define FPGA_CMD_GET_REGS32 (0xEE)
#define FPGA_CMD_READ_CMDFIFO_DATA (0xFB)
#define FPGA_CMD_READID1 (0x9E)
#define FPGA_CMD_READID2 (0x9F)


#define PIN_NUM_SWITCH 34
#define PIN_NUM_CMD_INTERRUPT 27
#define PIN_NUM_SPECT_INTERRUPT 26
#define PIN_NUM_USB_INTERRUPT 22

class InterfaceZ: public QObject
{
    Q_OBJECT
public:

    struct Client
    {
        Client(InterfaceZ*me): intf(me) {
        }
        void gpioEvent(uint8_t);
        QTcpSocket *s;
        InterfaceZ *intf;
        uint8_t m_hdlcrxbuf[8192];
        hdlc_decoder_t m_hdlc_decoder;
        hdlc_encoder_t m_hdlc_encoder;
        QByteArray m_txarray;
    };



    InterfaceZ();
    int init();
    UCHAR ioread(UCHAR address);
    void iowrite(UCHAR address, UCHAR value);
    void loadCustomROM(const char *file);

    static void hdlcDataReady(void *user, const uint8_t *data, unsigned len);
    void hdlcDataReady(Client *c, const uint8_t *data, unsigned len);

    static void hdlc_writer(void *userdata, const uint8_t c);
    void hdlc_writer(const uint8_t c);
    static void hdlc_flusher(void *userdata);
    void hdlc_flusher(void);


    public slots:
    void newConnection();
    void readyRead(Client*);
    void socketError(Client*, QAbstractSocket::SocketError);
    //void onSDConnected();
    //void onSDDisconnected();
    //void WiFiScanFinished();
    //void WiFiConnected();
    void onNMI();
protected:
    void fpgaCommandReadID(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaSetFlags(const uint8_t *data, int datalen, uint8_t *txbuf);
    void fpgaWriteROM(const uint8_t *data, int datalen, uint8_t *txbuf);
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
    void cmdFifoWriteEvent();

    class DataShortException : public std::exception {
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
signals:
    void tapDataReady();

private:
    QTcpServer *m_fpgasocket;
    uint8_t customrom[16384];
    bool customromloaded;
    uint8_t saverom[16384];

    uint8_t extram[65536*2]; // Only 2 blocks for now.
    uint32_t extramptr;

    uint32_t regs[32];

    uint8_t m_sna[49179];
    unsigned m_sna_size;
    const uint8_t *m_sna_head;
    const uint8_t *m_sna_tail;
    bool m_sna_rom_uploaded;

    uint8_t m_sna_rom[16384];
    int m_sna_rom_size;

    QByteArray m_nmirom;

    QQueue<uint8_t> m_resourcefifo;
    QQueue<uint8_t> m_cmdfifo;
    QQueue<uint16_t> m_tapfifo;

    QList<Client*> m_clients;
    uint16_t fpga_flags;
};

#endif
