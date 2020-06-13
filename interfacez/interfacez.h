#ifndef __INTERFACEZ_H__
#define __INTERFACEZ_H__

#include <QTcpServer>
#include <QMap>
#include <QString>
#include <QQueue>
#include <QTimer>
#include <inttypes.h>
#include "expansion/expansion.h"

#include "interfacez/EmptyResource.h"
#include "interfacez/StringResource.h"
#include "interfacez/StatusResource.h"
#include "interfacez/DirectoryResource.h"
#include "interfacez/WiFiListResource.h"
#include "interfacez/OpStatusResource.h"
#include "interfacez/Int8Resource.h"
#include "interfacez/BinaryResource.h"

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
    InterfaceZ();
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
    void eval_command();
    void pushResource(const Resource &c);

    void registerBitmapImageResource(uint8_t id, const uint8_t *data, uint8_t width_bytes, uint8_t height_bits);
    void registerResource(uint8_t id, Resource *r);
    void startWiFiScan();

    void loadCustomROM(const char *file);
    void saveSNA();
public slots:
    void newConnection();
    void readyRead(QTcpSocket*);
    void socketError(QTcpSocket*, QAbstractSocket::SocketError);
    void onSDConnected();
    void onSDDisconnected();
    void WiFiScanFinished();
    void WiFiConnected();
    void onNMI();
private:
    QTcpServer *m_cmdsocket;
    command_t *cmdt;
    typedef int (InterfaceZ::*func_handler)(command_t*,int argc,char**argv);

    QMap<QString, func_handler > m_handlers;
    uint8_t customrom[16384];
    bool customromloaded;
    uint8_t saverom[16384];

    uint8_t extram[65536*2]; // Only 2 blocks for now.
    uint32_t extramptr;


    uint8_t m_sna[49179];
    unsigned m_sna_size;
    const uint8_t *m_sna_head;
    const uint8_t *m_sna_tail;
    bool m_sna_rom_uploaded;

    uint8_t m_sna_rom[16384];
    int m_sna_rom_size;

    QString m_version;
    QString m_apname;

    QQueue<uint8_t> m_resourcefifo;
    QMap<uint8_t, Resource *> m_resources;
    QQueue<uint8_t> m_cmdfifo;
    QByteArray m_nmirom;

    StringResource m_versionresource;
    EmptyResource m_invalidresource;
    StatusResource m_statusresource;
    DirectoryResource m_directoryresource;
    StringResource m_wificonfigresource;
    WiFiListResource m_wifilistresource;
    OpStatusResource m_opstatusresource;
    Int8Resource m_videomoderesource;
    uint8_t m_videomode;
    QList<WiFiListResource::AccessPoint> m_aplist;
    QTimer m_scantimer;
    QTimer m_connecttimer;
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
