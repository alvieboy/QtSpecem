#ifndef __INTERFACEZ_H__
#define __INTERFACEZ_H__

#include <QTcpServer>
#include <QMap>
#include <QString>
#include <QQueue>
#include <inttypes.h>

#include "expansion/expansion.h"
class QTcpSocket;

typedef enum {
    READCMD,
    READDATA
} cmdstate_t;

class InterfaceZ;

typedef struct command
{
    int socket;
    struct sockaddr_in *source_addr;
    int (InterfaceZ::*rxdatafunc)(struct command *cmdt);
    uint8_t tx_prebuffer[4];
    uint8_t rx_buffer[1024];
    unsigned len;
    unsigned romsize;
    unsigned romoffset;
    cmdstate_t state;
} command_t;


class InterfaceZ: public QObject
{
    Q_OBJECT
public:
    int init();
    UCHAR ioread(UCHAR address);
    void iowrite(UCHAR address, UCHAR value);
    void upload_sna_rom();
    // From ESP
    int check_command(command_t*, uint8_t*);

    // Handlers
    int cmd_reset(command_t *, int, char**);
    int cmd_upload_rom(command_t *, int, char**);
    int cmd_resettocustom(command_t *, int, char**);
    int cmd_upload_sna(command_t *, int, char**);
    int upload_rom_data(command_t*);
    int upload_sna_data(command_t *cmdt);
public slots:
    void newConnection();
    void readyRead(QTcpSocket*);
    void socketError(QTcpSocket*, QAbstractSocket::SocketError);


private:
    QTcpServer *m_cmdsocket;
    command_t *cmdt;
    typedef int (InterfaceZ::*func_handler)(command_t*,int argc,char**argv);

    QMap<QString, func_handler > m_handlers;
    uint8_t customrom[16384];
    uint8_t m_sna[49179];
    unsigned m_sna_size;
    const uint8_t *m_sna_head;
    const uint8_t *m_sna_tail;
    bool m_sna_rom_uploaded;
    uint8_t m_sna_rom[16384];
    int m_sna_rom_size;
    //QQueue<uint8_t> m_fifo;
};

#define FLAG_RSTFIFO (1<<0)
#define FLAG_RSTSPECT (1<<1)
#define FLAG_CAPCLR (1<<2)
#define FLAG_CAPRUN (1<<3)
#define FLAG_COMPRESS (1<<4)
#define FLAG_ENABLE_INTERRUPT (1<<5)
#define FLAG_CAPSYNCEN (1<<6)
#define FLAG_FORCEROMCS (1<<7)


#endif
