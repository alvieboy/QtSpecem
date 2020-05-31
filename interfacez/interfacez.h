#ifndef __INTERFACEZ_H__
#define __INTERFACEZ_H__

#include <QTcpServer>
#include <QMap>
#include <QString>
#include <QQueue>
#include <QTimer>
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


#define RESOURCE_TYPE_INVALID (0xff)
#define RESOURCE_TYPE_INTEGER (0x00)
#define RESOURCE_TYPE_STRING (0x01)
#define RESOURCE_TYPE_BITMAP (0x02)
#define RESOURCE_TYPE_DIRECTORYLIST (0x03)
#define RESOURCE_TYPE_APLIST (0x04)

struct Resource
{
    virtual uint8_t type() const = 0;
    virtual uint16_t len() const = 0;
    virtual void copyTo(QQueue<uint8_t> &dest) const = 0;
};

struct StringResource: public Resource
{
    StringResource(QString &s): m_str(s){
    }
    uint8_t type() const override { return 0x01; }
    uint16_t len() const override { return m_str.length() + 1; }

    void copyTo(QQueue<uint8_t> &queue) const override{
        queue.push_back(m_str.length() & 0xff);
        for (auto i: m_str) {
            queue.push_back(i.toLatin1());
        }
    }
private:
    QString &m_str;
};

struct BinaryResource: public Resource
{
    BinaryResource(const uint8_t type, const uint8_t *data, unsigned len): m_type(type), m_data((const char*)data,len) {
    }
    BinaryResource(const uint8_t type, const QByteArray &data): m_type(type), m_data(data) {
    }
    uint8_t type() const override { return m_type; }
    uint16_t len() const override { return m_data.length(); }
    void copyTo(QQueue<uint8_t> &queue) const override{
        for (auto i: m_data) {
            queue.push_back(i);
        }
    }
private:
    uint8_t m_type;
    QByteArray m_data;
};

struct EmptyResource: public Resource
{
    EmptyResource(const uint8_t type): m_type(type) {
    }
    uint8_t type() const override { return m_type; }
    uint16_t len() const override { return 0; }
    void copyTo(QQueue<uint8_t> &queue) const override{
        Q_UNUSED(queue)
    }
private:
    uint8_t m_type;
};

struct Int8Resource: public Resource
{
    Int8Resource(const uint8_t *val): m_ptr(val) {
    }
    uint8_t type() const override { return 0; }
    uint16_t len() const override { return 1; }
    void copyTo(QQueue<uint8_t> &queue) const override{
        queue.push_back(*m_ptr);
        qDebug()<<"VIDEO MODE "<<(*m_ptr);
    }
private:
    const uint8_t *m_ptr;
};

struct StatusResource: public Resource
{
    StatusResource(): m_val(0) {
    }
    uint8_t type() const override { return RESOURCE_TYPE_INTEGER; }
    uint16_t len() const override { return 1; }
    void set(uint8_t val) {
        m_val = val;
    }
    void setbit(uint8_t bit)
    {
        m_val = m_val | (1<<bit);
    }
    void clearbit(uint8_t bit)
    {
        m_val = m_val & ~(1<<bit);
    }

    uint8_t get() const {
        return m_val;
    }
    void copyTo(QQueue<uint8_t> &queue) const override{
        qDebug()<<"Sta "<<m_val;
        queue.push_back( m_val );
    }
private:
    uint8_t m_val;
};

struct DirectoryResource: public Resource
{
    DirectoryResource();
    uint8_t type() const override;
    uint16_t len() const override;
    void copyTo(QQueue<uint8_t> &queue) const override;

#define FLAG_FILE 0
#define FLAG_DIRECTORY 1

    struct fileentry {
        fileentry(uint8_t f, const QString &n): flags(f), name(n) {
        }
        uint8_t flags;
        QString name;
    };
    QList<struct fileentry> m_files;
    QString m_cwd;
};

struct AccessPoint
{
    AccessPoint(uint8_t f, const QString &ss): flags(f), ssid(ss) {}
    uint8_t flags;
    QString ssid;
};


struct WiFiListResource: public Resource
{
    WiFiListResource(QList<AccessPoint>&aplist): m_accesspoints(aplist) {}
    uint8_t type() const override { return RESOURCE_TYPE_APLIST; }
    uint16_t len() const override;
    void copyTo(QQueue<uint8_t> &queue) const override;
private:
    QList<AccessPoint> &m_accesspoints;
};

struct OpStatusResource: public Resource
{
    OpStatusResource(): m_val(0xff) {
    }
    uint8_t type() const override { return 0x05; }
    uint16_t len() const override { return m_str.length() + 2; }

    void copyTo(QQueue<uint8_t> &queue) const override{
        printf("Reading status: %d\n", m_val);
        queue.push_back(m_val);
        queue.push_back(m_str.length() & 0xff);
        for (auto i: m_str) {
            queue.push_back(i.toLatin1());
        }
    }
    void setStatus(uint8_t val, const QString &msg){
        m_val=val;
        m_str = msg;
    }
private:
    uint8_t m_val;
    QString m_str;
};


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
    QList<AccessPoint> m_aplist;
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

#include <QFile>

class SnaFile
{
public:
    SnaFile(const QString &n): m_file(n)
    {
    }
    int open();
    void write(const uint8_t val);
    void write(const uint8_t *buf, size_t len);
    void close();
private:
    QFile m_file;
};

#endif
