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

#define LOGONAME logo2
#include "logo2.xbm"


#define STATUS_BIT_WIFI_MODE_STA 0
#define STATUS_BIT_WIFI_CONNECTED 1
#define STATUS_BIT_WIFI_SCANNING 2
#define STATUS_BIT_SDCONNECTED 3

static const uint8_t bitRevTable[256] =
{
    0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
    0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
    0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
    0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
    0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
    0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
    0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
    0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
    0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
    0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
    0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
    0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
    0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
    0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
    0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
    0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
    0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
    0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
    0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
    0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
    0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
    0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
    0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
    0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
    0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
    0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
    0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
    0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
    0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
    0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
    0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
    0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
};



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



UCHAR interfacez__ioread(void*user,UCHAR address)
{
    return static_cast<InterfaceZ*>(user)->ioread(address);
}

void interfacez__iowrite(void*user,UCHAR address, UCHAR value)
{
    static_cast<InterfaceZ*>(user)->iowrite(address, value);
}

InterfaceZ::InterfaceZ():
m_version("v1.0 2020/03/30 FPGA A5.03 r3"),
m_apname("[MyHouseWiFi]"),
m_versionresource(m_version),
m_invalidresource(RESOURCE_TYPE_INVALID),
m_wificonfigresource(m_apname),
m_wifilistresource(m_aplist),
m_videomoderesource(&m_videomode)
{
    m_videomode = 2;
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
    if (r<0)
        return -1;

    m_handlers["reset"] = &InterfaceZ::cmd_reset;
    m_handlers["uploadrom"] = &InterfaceZ::cmd_upload_rom;
    m_handlers["uploadsna"] = &InterfaceZ::cmd_upload_sna;
    m_handlers["resettocustom"] = &InterfaceZ::cmd_resettocustom;


    registerResource( 0x00, &m_versionresource);
    registerBitmapImageResource(0x01, logo2_bits, logo2_width>>3, logo2_height);
    registerResource( 0x02, &m_statusresource);
    registerResource( 0x03, &m_directoryresource);
    registerResource( 0x04, &m_wificonfigresource);
    registerResource( 0x05, &m_wifilistresource);
    registerResource( 0x06, &m_videomoderesource);
    registerResource( 0x10, &m_opstatusresource);

    cmdt = new command_t; // THIS MUST BE by socket

    m_cmdsocket = new QTcpServer();

    connect(m_cmdsocket, &QTcpServer::newConnection, this, &InterfaceZ::newConnection);



    if (!m_cmdsocket->listen(QHostAddress::Any, 8003)) { // QTcpSocket::ReuseAddressHint
        qDebug()<<"Cannot listen";
        return -1;
    }

    m_sna_rom_size = -1;

    // Test
    m_statusresource.set(0x03); // Connected, STA mode

    connect(&m_scantimer, &QTimer::timeout, this, &InterfaceZ::WiFiScanFinished);
    connect(&m_connecttimer, &QTimer::timeout, this, &InterfaceZ::WiFiConnected);
    return 0;
}

void InterfaceZ::newConnection()
{
    QTcpSocket *s = m_cmdsocket->nextPendingConnection();

    connect(s, &QTcpSocket::readyRead, this, [this,s](){ this->readyRead(s); });
    connect(s, qOverload<QAbstractSocket::SocketError>(&QAbstractSocket::error),
            this,[this,s](QAbstractSocket::SocketError error){this->socketError(s, error);});
    qDebug()<<"New connection";
    cmdt->len = 0;
    cmdt->state = READCMD;

    qDebug()<<"New connection2";
}


void InterfaceZ::socketError(QTcpSocket *s, QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    qDebug()<<"Socket error"<<s->error();
}

UCHAR InterfaceZ::ioread(UCHAR address)
{
    uint8_t val = 0xff;


    switch (address) {
    case 0x05:
        val = 0x39; //Bg
        break;
    case 0x07: // Cmd fifo status
        if (m_cmdfifo.size()>=32) {
            val = 0x01;
        } else {
            val = 0x00;
        }
        break;
    case 0x0B:
        if (m_resourcefifo.empty()) {
            val = 0x01;
        } else {
            val = 0x00;
        }
        break;

    case 0x0D:
        if (m_resourcefifo.empty()) {
            val = 0x00;
        } else {
            val = m_resourcefifo.front();
            m_resourcefifo.pop_front();
        }

        break;
    case 0x17:
        printf("return RAM[0x%06x] = 0x%02x\n", extramptr, extram[extramptr]);
        val = extram[extramptr++];
        break;

    case 0x1F: // Joy port
        val = 0x00;
        break;
    }

    return val;
}




void InterfaceZ::pushResource(const Resource &r)
{
    unsigned len = r.len();
    m_resourcefifo.push_back(r.type());
    if (r.type()!=RESOURCE_TYPE_INVALID) {
        m_resourcefifo.push_back(len & 0xff);
        m_resourcefifo.push_back((len>>8) & 0xff);

        r.copyTo(m_resourcefifo);
    }
}

void InterfaceZ::eval_command()
{
    uint8_t filelen;
    //uint8_t status;
    int psize;
    // Simple for now
    uint8_t v = m_cmdfifo.front();
    switch (v) {
    case 0x00: /* Load resource */
        if (m_cmdfifo.size()<2)
            break; // Need more data
        m_cmdfifo.pop_front();
        v = m_cmdfifo.front(); // Get resource ID
        m_cmdfifo.pop_front();

        if (m_resources.find(v) == m_resources.end()) {
            pushResource(m_invalidresource);
        } else {
            pushResource(*m_resources[v]);
        }
        break;
    case 0x01:
        /* Set AP */
        {
            if (m_cmdfifo.size()<3)
                return;
            // 0: command
            // 1: WiFI mode (0: STA mode, 1: AP mode)
            // 2: SSID len
            // 3: ssid0
            // 4: ssid1
            // 5: pwlen
            // 6: pw1
            // 7: pw2
            uint8_t ssidlen = m_cmdfifo[2];
            int psize = 3+ssidlen;
            if (m_cmdfifo.size() < (psize+1)) // 3 bytes, SSID chars, PWD size.
                return;
            uint8_t pwdlen = m_cmdfifo[psize];

            if (m_cmdfifo.size() < psize+1+pwdlen) {
                return;
            }
            // Fetch data.
            int idx = 1;
            QString ssid, password;
            //uint8_t mode = m_cmdfifo[idx++];
            idx++; // past ssidlen

            while (ssidlen--) {
                ssid+=m_cmdfifo[idx++];
            }
            idx++; // past pwdlen
            while (pwdlen--) {
                password+=m_cmdfifo[idx++];
            }

            qDebug()<<"Connect to SSID"<<ssid<<"with password"<<password;

            m_cmdfifo.clear();
            m_statusresource.clearbit(STATUS_BIT_WIFI_CONNECTED);
            m_statusresource.setbit(STATUS_BIT_WIFI_MODE_STA);
            m_apname = ssid;
            m_connecttimer.start(2000);
        }


        break;
    case 0x02:
        /* Start scanning process */
        m_cmdfifo.pop_front();
        startWiFiScan();
        break;

    case 0x05:
        /* Save SNA */
        if (m_cmdfifo.size()<2)
            break; // Need more data

        filelen = m_cmdfifo[1];
        psize = 2+filelen;

        if (m_cmdfifo.size() < psize)
            break;
        printf("Saving snapshot file len %d\n", filelen);
        //
        //m_opstatusresource.setStatus(0xFE, ""); // Set operation in progress
        saveSNA();
        m_cmdfifo.clear();
        break;

    case 0x08:
        /* Play tape */
        if (m_cmdfifo.size()<2)
            break; // Need more data

        filelen = m_cmdfifo[1];
        psize = 2+filelen;

        if (m_cmdfifo.size() < psize)
            break;
        printf("Play tape\n");
        m_cmdfifo.clear();
        break;

    case 0x07:// File filter
        if (m_cmdfifo.size()<2)
            break; // Need more data
        printf("File filter set to %d\n", m_cmdfifo[1]);
        m_cmdfifo.clear();
        break;

    case 0x0C:// Video mode */
        if (m_cmdfifo.size()<2)
            break; // Need more data
        qDebug()<<"VIDEO MODE SET "<<m_cmdfifo[1];
        m_videomode = m_cmdfifo[1];
        m_cmdfifo.clear();
        break;
    default:
        //pushResource(m_invalidresource);
        m_opstatusresource.setStatus(0xff,"Unknown command");
        printf("Received INVALID command %02x\n",v);
        break;
    }
}

void InterfaceZ::iowrite(UCHAR address, UCHAR value)
{
    switch (address) {
    case 0x09: // Cmd fifo
        if (m_cmdfifo.size()<32) {
            m_cmdfifo.push_back(value);
            eval_command();
        }
        break;
    case 0x11:
        // Address LSB
        extramptr &= 0xFFFF00;
        extramptr |= value;
        break;

    case 0x13:
        // Address hSB
        extramptr &= 0xFF00FF;
        extramptr |= ((uint32_t)value)<<8;
        printf("EXT ram pointer: %06x\n", extramptr);
        break;

    case 0x15:
        // Address MSB
        extramptr &= 0x00FFFF;
        // We only use 1 banks.
        value &= 0x1;

        extramptr |= ((uint32_t)value)<<16;
        printf("EXT ram pointer: %06x\n", extramptr);
        break;

    case 0x17:
        printf("RAM[0x%06x] = 0x%02x\n", extramptr, value);
        extram[extramptr++] = value;
        if (extramptr>=sizeof(extram)) {
            extramptr=0;
        }
        break;

    }
}

void InterfaceZ::readyRead(QTcpSocket *s)
{
    qDebug()<<"Reading data";
    int len;
    do {
        len = s->read( (char*)&cmdt->rx_buffer[cmdt->len], sizeof(cmdt->rx_buffer) - (cmdt->len));

        uint8_t *nl = NULL;
        int r;

        if (len<0) {
            return;
        }

        cmdt->len += len;
        bool redo;

        do {
            redo=false;
            switch (cmdt->state) {
            case READCMD:
                // Looking for newline. NEEDS to be '\n';
                nl = (uint8_t*)memchr( cmdt->rx_buffer, '\n', cmdt->len);
                if (!nl) {
                    // So far no newline.
                    if (cmdt->len >= sizeof(cmdt->rx_buffer)) {
                        // Overflow, we cannot store more data.
                        s->close();
                        return;
                    }
                    // Just continue to get more data.
                } else {
                    r = check_command(cmdt, nl);
                    if (r<0) {
                        s->close();
                        return;
                    }
                    if (r==1) {
                        // We might have still data to process;
                        qDebug()<<"Recheck";
                        redo = true;
                    }
                    if (r==0) {
                        s->close();
                        return;
                    }
                }
                break;
            case READDATA:
                r = (this->*cmdt->rxdatafunc)(cmdt);
                if (r<0) {
                    s->close();
                    return;
                }
                if (r==0) {
                    s->close();
                    return;
                }

                // Default continue getting data
                redo = true;
                break;
            }
        } while (redo && cmdt->len);
    } while (len);
}

int InterfaceZ::check_command(command_t *cmdt, uint8_t *nl)
{
    int r;
    // Terminate it.
    *nl++ = '\0';

#define MAX_TOKS 8

    char *toks[MAX_TOKS];
    int tokidx = 0;
    char *p = (char*)&cmdt->rx_buffer[0];

    while ((toks[tokidx] = strtok_r(p, " ", &p))) {
        tokidx++;
        if (tokidx==MAX_TOKS) {
            return -1;
        }
    }

    //int thiscommandlen = strlen(toks[0]);


    QMap<QString, func_handler >::iterator h = m_handlers.find(toks[0]);

    if (h!=m_handlers.end()) {

        r = (this->*(*h))( cmdt, tokidx-1, &toks[1] );

        // TBD: copy extra data.
        //ESP_LOGI(TAG, "NL disparity %d len is %d\n", nl - (&cmdt->rx_buffer[0]), cmdt->len);
        int remain  = cmdt->len - (nl - (&cmdt->rx_buffer[0]));
        if (remain>0) {
            memmove(&cmdt->rx_buffer[0], nl, remain);
            cmdt->len = remain;
        } else {
            // "Eat"
            cmdt->len = 0;
        }
        return r;
    }
    qDebug()<<"Cannot find command handler for"<<toks[0];
    return -1;
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

int InterfaceZ::cmd_reset(command_t *, int, char**)
{
    qDebug()<<"Reset";
    do_reset();
    return 0;
}

int InterfaceZ::cmd_resettocustom(command_t *, int, char**)
{
    qDebug()<<"Reset to custom";
    set_current_rom(&customrom[0]);

    do_reset();
    //enable_trace();
    return 0;
}



int InterfaceZ::cmd_upload_rom(command_t *cmdt, int argc, char **argv)
{
    int romsize;
    qDebug()<<"cmd_upload_rom";
    if (argc<1) {
        qDebug()<<"Args error";
        return -1;
    }

    // Extract size from params.
    if (strtoint(argv[0], &romsize)<0) {
        qDebug()<<"Size error";
        return -1;
    }
    if (romsize > 16384) {
        return -1;
    }

    m_sna_rom_uploaded = false;

    cmdt->romsize = romsize;
    cmdt->romoffset = 0;
    cmdt->rxdatafunc = &InterfaceZ::upload_rom_data;
    cmdt->state = READDATA;

    return 1; // Continue receiving data.
}


int InterfaceZ::cmd_upload_sna(command_t *cmdt, int argc, char **argv)
{
    int romsize;
    qDebug()<<"cmd_upload_sna";
    if (argc<1) {
        qDebug()<<"Args error";
        return -1;
    }

    // Extract size from params.
    if (strtoint(argv[0], &romsize)<0) {
        qDebug()<<"Size error";
        return -1;
    }

    if ((unsigned)romsize > sizeof(m_sna)) {
        qDebug()<<"Image too big "<<romsize<<"for"<<sizeof(m_sna);
        return -1;
    }
    m_sna_size = romsize;
    cmdt->romsize = romsize;
    cmdt->romoffset = 0;
    cmdt->rxdatafunc = &InterfaceZ::upload_sna_data;
    m_sna_head = &m_sna[27];
    m_sna_tail = &m_sna[27];
    cmdt->state = READDATA;

    return 1; // Continue receiving data.
}

int InterfaceZ::upload_rom_data(command_t *cmdt)
{
    unsigned remain = cmdt->romsize - cmdt->romoffset;

    if (remain<cmdt->len) {
        // Too much data, complain
        qDebug()<<"ROM: expected max"<<remain<<"but got"<<cmdt->len<<"bytes";
        return -1;
    }

    memcpy( &customrom[cmdt->romoffset], cmdt->rx_buffer, cmdt->len);

    cmdt->romoffset += cmdt->len;

    qDebug()<<"ROM: offset"<<cmdt->romoffset<<"after uploading"<<cmdt->len<<"bytes";

    cmdt->len = 0; // Reset receive ptr.

    remain = cmdt->romsize - cmdt->romoffset;

    if (remain==0)
        return 0;

    {
        printf("Dump: [");
        for (int i=0;i<16;i++) {
            printf(" %02x", customrom[i]);
        }
        printf(" ]\n");
    }

    return 1;
}

void InterfaceZ::upload_sna_rom()
{
    // Load ROM

    if (m_sna_rom_size<0) {
        QFile file(":/rom/snaloader.rom");

        if(file.open(QIODevice::ReadOnly)){
            QByteArray data=file.readAll();
            file.close();
            m_sna_rom_size = data.length();
            memcpy( m_sna_rom, data.constData(), m_sna_rom_size);
        } else {
            qDebug()<<"Cannot load ROM file";
            return;
        }
    }
    sna_apply_relocs(m_sna, &m_sna_rom[0]);
    qDebug()<<"Applying custom SNA rom";
    set_current_rom(&m_sna_rom[0]);

    do_reset();
    //enable_trace();

    m_sna_rom_uploaded = true;
}

int InterfaceZ::upload_sna_data(command_t *cmdt)
{
    unsigned remain = cmdt->romsize - cmdt->romoffset;

    if (remain<cmdt->len) {
        // Too much data, complain
        qDebug()<<"SNA: expected max"<<remain<<"but got"<<cmdt->len<<"bytes";
        return -1;
    }

    memcpy( &m_sna[cmdt->romoffset], cmdt->rx_buffer, cmdt->len);

    cmdt->romoffset += cmdt->len;

    qDebug()<<"SNA: offset"<<cmdt->romoffset<<"after uploading"<<cmdt->len<<"bytes";

    cmdt->len = 0; // Reset receive ptr.

    remain = cmdt->romsize - cmdt->romoffset;

    {
        printf("Dump: [");
        for (int i=0;i<16;i++) {
            printf(" %02x", customrom[i]);
        }
        printf(" ]\n");
    }
    // Update tail pointer.
    if (cmdt->romoffset > 27) {
        if (!m_sna_rom_uploaded) {
            upload_sna_rom();
        }
        m_sna_tail = &m_sna[cmdt->romoffset];
        printf("Current used space: %ld\n", m_sna_tail - m_sna_head);
    }

    if (remain==0)
        return 0;


    return 1;
}



#if 0

void set_capture_mask(uint32_t mask)
{
    set_register(REG_CAPTURE_MASK, mask);
}

void set_capture_value(uint32_t value)
{
    set_register(REG_CAPTURE_VAL, value);
}


volatile int client_socket = -1;

#define MAX_FRAME_PAYLOAD 2048

struct frame {
    uint8_t seq;
    uint8_t frag;
    uint8_t payload[MAX_FRAME_PAYLOAD];
};

static struct frame f; // ensure we have stack!!!

static int fill_and_send_frames(int client_socket,
                                uint8_t seq,
                                const uint8_t *data)
{
    f.seq = seq;

    unsigned off = 0;
    uint8_t frag = 0;
    unsigned size = SPECTRUM_FRAME_SIZE;
    while (size) {
        unsigned chunk = size > MAX_FRAME_PAYLOAD?MAX_FRAME_PAYLOAD:size;
        f.frag = frag;

        //ESP_LOGI(TAG, "Chunk %d off %d frag %d", chunk, off, frag);

        memcpy(f.payload, &data[off], chunk);

        off+=chunk;
        size-=chunk;
        chunk += 2; // Size of header

        if (send( client_socket, &f, chunk, 0)!=chunk) {
            return -1;
        }
        frag++;
    }
    return 0;
}

static void streamer_server_task(void *pvParameters)
{
    uint32_t io_num;
    uint8_t seqno = 0;
    do {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            get_framebuffer(fb);
            if (client_socket>0) {
                //ESP_LOGI(TAG, "Sending frames sock %d", client_socket);
                if (fill_and_send_frames( client_socket, seqno++, &fb[4])<0) {
                    ESP_LOGE(TAG, "Error sending to client socket");
                    shutdown(client_socket,0);
                    close(client_socket);
                    client_socket = -1;
                }
            }
            seqno++;
        }
    } while (1);
}

static int do_reset_spectrum(command_t *cmdt, int argc, char **argv, bool forcerom)
{
    bool do_cap = false;
    bool has_mask = false;
    bool has_value = false;

    uint32_t mask;
    uint32_t value;

    int argindex=0;
    ESP_LOGI(TAG, "Argc %d\n", argc);
    // Check if we have capture
    if (argc>0) {
        if (strcmp(argv[argindex], "cap")==0) {
            argindex++;
            argc--;
            do_cap=true;
        }
    }
    if (do_cap && argc>0) {
        // Mask
        ESP_LOGI(TAG, "Has mask");
        if (strtoint(argv[argindex], (int*)&mask)==0) {
            has_mask=true;
            argindex++;
            argc--;
        } else {
            return -1;
        }
    }

    if (has_mask && argc>0) {
        // Mask
        ESP_LOGI(TAG, "Has value");
        if (strtoint(argv[argindex], (int*)&value)==0) {
            has_value=true;
            argindex++;
            argc--;
        } else {
            return -1;
        }
    }

    if (forcerom)
        ESP_LOGI(TAG, "Resetting spectrum (to custom ROM)");
    else
        ESP_LOGI(TAG, "Resetting spectrum (to internal ROM)");


    if (has_mask) {
        ESP_LOGI(TAG, "Enabling capture mask 0x%08x", mask);
        set_capture_mask(mask);
    }

    if (has_value) {
        ESP_LOGI(TAG, "Enabling capture value 0x%08x", value);
        set_capture_value(value);
    }
    if (do_cap) {

//#define FLAG_CAPCLR (1<<2)
//#define FLAG_CAPRUN (1<<3)
//#define FLAG_COMPRESS (1<<4)

        set_flags(FLAG_RSTSPECT | FLAG_CAPCLR );
        if (forcerom)
            set_flags(FLAG_FORCEROMCS);
        else
            clear_flags(FLAG_FORCEROMCS);

        vTaskDelay(2 / portTICK_RATE_MS);
        set_clear_flags(FLAG_CAPRUN, FLAG_CAPCLR | FLAG_RSTSPECT | FLAG_COMPRESS);

    } else {
        set_flags(FLAG_RSTSPECT | FLAG_CAPCLR);
        if (forcerom)
            set_flags(FLAG_FORCEROMCS);
        else
            clear_flags(FLAG_FORCEROMCS);

        vTaskDelay(2 / portTICK_RATE_MS);
        clear_flags(FLAG_RSTSPECT);
    }
    ESP_LOGI(TAG, "Reset completed");
    return 0;
}

static int reset_spectrum(command_t *cmdt, int argc, char **argv)
{
    return do_reset_spectrum(cmdt, argc, argv, 0);
}

static int reset_custom_spectrum(command_t *cmdt, int argc, char **argv)
{
    return do_reset_spectrum(cmdt, argc, argv, 1);
}


static uint32_t get_capture_status()
{
    uint8_t buf[6];
    buf[0] = 0xE2;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x00;

    spi__transceive(spi0_fpga, buf, sizeof(buf));
    uint32_t ret = getbe32( &buf[2] );
    return ret;
}

static int send_captures(command_t *cmdt, int argc, char **argv)
{
    // Get capture flags

    uint32_t flags = get_capture_status();
    ESP_LOGI(TAG,"Capture flags: 0x%08x", flags);

    fb[0] = 0xE0;
    fb[1] = 0x00;
    int nlen = 2+ (5 * 2048);
    int len = -1;
    ESP_LOGI(TAG, "Transceiving %d bytes\r\n", nlen);

    spi__transceive(spi0_fpga, fb, nlen);
    const uint8_t *txptr = &fb[2];

    nlen -= 2;

    // Find limit.


    do {
        len = send(cmdt->socket, txptr, nlen, 0);
        if (len>0) {
            nlen -= len;
        } else {
            break;
        }
    } while (nlen>0);

    if (len < 0) {
        ESP_LOGE(TAG, "send failed: errno %d", errno);
        return -1;
    }
    return 0;
}



int start_stream(command_t *cmdt, int argc, char **argv)
{
    int port;
    struct sockaddr_in s;
    int r = -1;

    if (cmdt->source_addr->sin_family!=AF_INET)
        return r;

    if (argc<1)
        return r;

    do {
        if (strtoint(argv[0], &port)<0) {
            break;
        }

        // create socket.
        int tsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (tsock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }

        s.sin_family = AF_INET;
        s.sin_port = htons(port);

        s.sin_addr = cmdt->source_addr->sin_addr;

        int err = connect(tsock, (struct sockaddr *)&s, sizeof(s));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }

        ESP_LOGI(TAG, "Socket created, %s:%d", inet_ntoa(s.sin_addr.s_addr), (int)port);
        client_socket = tsock;
        r = 0;

    } while (0);

    return r;
}

static int upload_rom_data(command_t *cmdt)
{
    unsigned remain = cmdt->romsize - cmdt->romoffset;

    if (remain<cmdt->len) {
        // Too much data, complain
        ESP_LOGE(TAG, "ROM: expected max %d but got %d bytes", remain, cmdt->len);
        return -1;
    }

    // Upload chunk
    cmdt->tx_prebuffer[1] = 0xE1;
    cmdt->tx_prebuffer[2] = (cmdt->romoffset>>8) & 0xFF;
    cmdt->tx_prebuffer[3] = cmdt->romoffset & 0xFF;

    {
        dump__buffer(&cmdt->tx_prebuffer[1], 8);
    }

    spi__transceive(spi0_fpga, &cmdt->tx_prebuffer[1], 3 + cmdt->len);

    cmdt->romoffset += cmdt->len;

    ESP_LOGI(TAG, "ROM: offset %d after uploading %d bytes", cmdt->romoffset, cmdt->len);

    cmdt->len = 0; // Reset receive ptr.

    remain = cmdt->romsize - cmdt->romoffset;

    if (remain==0)
        return 0;

    return 1;
}

static int upload_rom(command_t *cmdt, int argc, char **argv)
{
    int romsize;

    if (argc<1)
        return -1;

    // Extract size from params.
    if (strtoint(argv[0], &romsize)<0) {
        return -1;
    }
    if (romsize > 16384)
        return -1;

    cmdt->romsize = romsize;
    cmdt->romoffset = 0;
    cmdt->rxdatafunc = &upload_rom_data;
    cmdt->state = READDATA;

    return 1; // Continue receiving data.
}

static int scap(command_t *cmdt, int argc, char **argv)
{
    if (argc>0) {
        ESP_LOGI(TAG, "Putting spectrum under RESET");
        set_flags(FLAG_RSTSPECT);
    }

    ESP_LOGE(TAG, "Capture mask: %08x",get_register( REG_CAPTURE_MASK ));
    ESP_LOGE(TAG, "Capture value: %08x",get_register( REG_CAPTURE_VAL ));
    ESP_LOGE(TAG, "r2: %08x", get_register( 2 ));
    ESP_LOGE(TAG, "r3: %08x", get_register( 3 ));
    //set_capture_mask(0x0);
    //set_capture_value(0x0);

    clear_flags(FLAG_CAPRUN | FLAG_COMPRESS);
    set_flags(FLAG_CAPCLR);
    set_clear_flags(FLAG_CAPRUN, FLAG_CAPCLR);
    ESP_LOGI(TAG, "Starting forced capture");

    vTaskDelay(500 / portTICK_RATE_MS);

    uint32_t stat = get_capture_status();
    ESP_LOGI(TAG, "Capture status: %08x\n", stat);

    if (argc>0) {
        ESP_LOGI(TAG, "Releasing spectrum from RESET");
        clear_flags(FLAG_RSTSPECT);
    }
    return 0;
}

struct commandhandler_t hand[] = {
    { CMD("fb"), &send_framebuffer },
    { CMD("cap"), &send_captures },
    { CMD("stream"), &start_stream },
    { CMD("uploadrom"), &upload_rom },
    { CMD("reset"), &reset_spectrum },
    { CMD("scap"), &scap },
    { CMD("resettocustom"), &reset_custom_spectrum },
};


static int check_command(command_t *cmdt, uint8_t *nl)
{
    int i;
    struct commandhandler_t *h;
    int r;
    // Terminate it.
    *nl++ = '\0';

#define MAX_TOKS 8

    char *toks[MAX_TOKS];
    int tokidx = 0;
    char *p = (char*)&cmdt->rx_buffer[0];

    while ((toks[tokidx] = strtok_r(p, " ", &p))) {
        tokidx++;
        if (tokidx==MAX_TOKS) {
            return -1;
        }
    }

    int thiscommandlen = strlen(toks[0]);


    for (i=0; i<sizeof(hand)/sizeof(hand[0]); i++) {
        h = &hand[i];
        if (h->cmdlen == thiscommandlen) {
            if (strcmp(h->cmd, toks[0])==0) {
                r = h->handler( cmdt, tokidx-1, &toks[1] );

                // TBD: copy extra data.
                //ESP_LOGI(TAG, "NL disparity %d len is %d\n", nl - (&cmdt->rx_buffer[0]), cmdt->len);
                int remain  = cmdt->len - (nl - (&cmdt->rx_buffer[0]));
                if (remain>0) {
                    memmove(&cmdt->rx_buffer[0], nl, remain);
                    cmdt->len = remain;
                } else {
                    // "Eat"
                    cmdt->len = 0;
                }
                return r;
            }
        }
    }
    ESP_LOGE(TAG,"Could not find a command handler for %s", toks[0]);

#if 0
    if (strncmp(rx, "fb", 2)==0) {
        send_framebuffer( sock );
    } else if (strncmp(rx, "cap", 3)==0) {
        send_captures( sock );
    } else if (strncmp(rx, "stream ",7)==0) {
        start_stream((struct sockaddr_in*)&source_addr, &rx[7]);
    } else if (strncmp(rx, "uploadrom ",10)==0) {
        upload_rom((struct sockaddr_in*)&source_addr, &rx[10], len-10);
    }
#endif
    return -1;
}


#endif

void InterfaceZ::registerBitmapImageResource(uint8_t id, const uint8_t *data, uint8_t width_bytes, uint8_t height_bits)
{
    QByteArray bytes;
    bytes.reserve( (width_bytes * height_bits)
                  + 2 // Width+Height
                 );

    bytes.append((char)width_bytes);
    bytes.append((char)height_bits);

    int i;

    for (i=0;i<width_bytes*height_bits;i++) {
        bytes.append( (char)bitRevTable[*data++] );

    }
    bytes.append((const char*)data, width_bytes*height_bits);

    m_resources[id] = new BinaryResource(RESOURCE_TYPE_BITMAP, bytes);
}

void InterfaceZ::registerResource(uint8_t id, Resource *r)
{
    m_resources[id] = r;
}


void InterfaceZ::onSDConnected()
{
    m_statusresource.setbit(STATUS_BIT_SDCONNECTED);
}

void InterfaceZ::onSDDisconnected()
{
    m_statusresource.clearbit(STATUS_BIT_SDCONNECTED);
}

void InterfaceZ::startWiFiScan()
{
    m_statusresource.setbit(STATUS_BIT_WIFI_SCANNING);
    m_scantimer.start(2000);
    m_aplist.clear();
}

DirectoryResource::DirectoryResource()
{
    m_cwd="/";
    m_files.push_back( fileentry(FLAG_DIRECTORY, "Games" ));
    m_files.push_back( fileentry(FLAG_FILE, "FILENONE.TAP" ));
    for (int i=0;i<4;i++) {
        char f[16];
        sprintf(f,"FILE%d.TAP", i);
        m_files.push_back( fileentry(FLAG_FILE, f));
    }
}

uint8_t DirectoryResource::type() const
{
    return RESOURCE_TYPE_DIRECTORYLIST;
}


uint16_t DirectoryResource::len() const
{
    unsigned len = 1; // Number of entries (one byte)
    len+= 1 + m_cwd.length();
    for (auto i: m_files) {
        len += 1; // Flags
        len += 1; // File name len
        len += i.name.length();
    }
    return len;
}

void DirectoryResource::copyTo(QQueue<uint8_t> &queue) const
{
    queue.push_back(m_files.size());
    for (auto c: m_cwd) {
        queue.push_back(c.toLatin1());
    }
    queue.push_back(0x00);

    for (auto i: m_files) {
        queue.push_back(i.flags);
        //queue.push_back(i.name.length());
        for (auto c: i.name) {
            queue.push_back(c.toLatin1());
        }
        queue.push_back(0x00);
    }
};

uint16_t WiFiListResource::len() const
{
    unsigned len = 1; // Number of entries (one byte)
    for (auto i: m_accesspoints) {
        len += 1; // Flags
        len += 1; // AP name len
        len += i.ssid.length();
    }
    return len;
}

void WiFiListResource::copyTo(QQueue<uint8_t> &queue) const
{
    queue.push_back(m_accesspoints.size());
    for (auto i: m_accesspoints) {
        queue.push_back(i.flags);
        //queue.push_back(i.name.length());
        for (auto c: i.ssid) {
            queue.push_back(c.toLatin1());
        }
        queue.push_back(0x00);
    }

};


void InterfaceZ::WiFiScanFinished()
{
    m_aplist.clear();
    m_aplist.push_back( WiFiListResource::AccessPoint(0x01, "Wifi1") );
    m_aplist.push_back( WiFiListResource::AccessPoint(0x01, "Wifi2") );
    m_aplist.push_back( WiFiListResource::AccessPoint(0x00, "OpenWifi3") );
    m_aplist.push_back( WiFiListResource::AccessPoint(0x01, "Wifi4TestLongName") );

    m_statusresource.clearbit(STATUS_BIT_WIFI_SCANNING);
}

void InterfaceZ::WiFiConnected()
{
    m_statusresource.setbit(STATUS_BIT_WIFI_CONNECTED);
}

void InterfaceZ::onNMI()
{
    printf(" ***** NMI ***** (custom: %s)\n", customromloaded?"yes":"no");
    trigger_nmi(customromloaded ? customrom : NULL);
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

#include "sna_defs.h"

void InterfaceZ::saveSNA()
{
    SnaFile sna("teste.sna");

    if (sna.open()<0) {
        m_opstatusresource.setStatus(0xFF, "Invalid filename"); // Set operation in progress
        return;
    }
    // Dump
    printf("***************** SNA info *******************\n");
    printf(" A  : %02x  F : %02x\n", extram[SNA_RAM_OFF_A], extram[SNA_RAM_OFF_F]);
    printf(" BC : %02x%02x\n", extram[SNA_RAM_OFF_B], extram[SNA_RAM_OFF_C]);
    printf(" DE : %02x%02x\n", extram[SNA_RAM_OFF_D], extram[SNA_RAM_OFF_E]);
    printf(" HL : %02x%02x\n", extram[SNA_RAM_OFF_H], extram[SNA_RAM_OFF_L]);

    printf(" SP : %02x%02x\n", extram[SNA_RAM_OFF_SPH], extram[SNA_RAM_OFF_SPL]);
    printf(" IX : %02x%02x\n", extram[SNA_RAM_OFF_IXH], extram[SNA_RAM_OFF_IXL]);
    printf(" IY : %02x%02x\n", extram[SNA_RAM_OFF_IYH], extram[SNA_RAM_OFF_IYL]);

    printf(" A' : %02x  F': %02x\n", extram[SNA_RAM_OFF_Aalt], extram[SNA_RAM_OFF_Falt]);
    printf(" BC': %02x%02x\n", extram[SNA_RAM_OFF_Balt], extram[SNA_RAM_OFF_Calt]);
    printf(" DE': %02x%02x\n", extram[SNA_RAM_OFF_Dalt], extram[SNA_RAM_OFF_Ealt]);
    printf(" HL': %02x%02x\n", extram[SNA_RAM_OFF_Halt], extram[SNA_RAM_OFF_Lalt]);

    printf(" R : %02x\n", extram[SNA_RAM_OFF_R]);
    printf(" IFF2: 0x%02x (%s)\n", extram[SNA_RAM_OFF_IFF2], extram[SNA_RAM_OFF_IFF2] & 4 ? "ENABLED" : "Disabled");


    //   0        1      byte   I                                      Check
    sna.write(extram[SNA_RAM_OFF_I]);
    //   1        8      word   HL',DE',BC',AF'                        Check
    sna.write(extram[SNA_RAM_OFF_Lalt]);
    sna.write(extram[SNA_RAM_OFF_Halt]);
    sna.write(extram[SNA_RAM_OFF_Ealt]);
    sna.write(extram[SNA_RAM_OFF_Dalt]);
    sna.write(extram[SNA_RAM_OFF_Calt]);
    sna.write(extram[SNA_RAM_OFF_Balt]);
    sna.write(extram[SNA_RAM_OFF_Falt]);
    sna.write(extram[SNA_RAM_OFF_Aalt]);
    //   9        10     word   HL,DE,BC,IY,IX                         Check
    sna.write(extram[SNA_RAM_OFF_L]);
    sna.write(extram[SNA_RAM_OFF_H]);
    sna.write(extram[SNA_RAM_OFF_E]);
    sna.write(extram[SNA_RAM_OFF_D]);
    sna.write(extram[SNA_RAM_OFF_C]);
    sna.write(extram[SNA_RAM_OFF_B]);
    sna.write(extram[SNA_RAM_OFF_IYL]);
    sna.write(extram[SNA_RAM_OFF_IYH]);
    sna.write(extram[SNA_RAM_OFF_IXL]);
    sna.write(extram[SNA_RAM_OFF_IXH]);
    //   19       1      byte   Interrupt (bit 2 contains IFF2, 1=EI/0=DI)  Check
    sna.write(extram[SNA_RAM_OFF_IFF2]);
    //   20       1      byte   R                                      Check
    sna.write(extram[SNA_RAM_OFF_R]);
    //   21       4      words  AF,SP                                  Check
    sna.write(extram[SNA_RAM_OFF_F]);
    sna.write(extram[SNA_RAM_OFF_A]);
    sna.write(extram[SNA_RAM_OFF_SPL]);
    sna.write(extram[SNA_RAM_OFF_SPH]);
    //   25       1      byte   IntMode (0=IM0/1=IM1/2=IM2)            Check
    sna.write(extram[SNA_RAM_OFF_IMM]);
    //   26       1      byte   BorderColor (0..7, not used by Spectrum 1.7)  Check
    sna.write(extram[SNA_RAM_OFF_BORDER]);
    //   27       49152  bytes  RAM dump 16384..65535
    sna.write(&extram[SNA_RAM_OFF_CHUNK1], SNA_RAM_CHUNK1_SIZE);

    sna.write(&extram[SNA_RAM_OFF_CHUNK2], SNA_RAM_CHUNK2_SIZE);
    sna.close();
    m_opstatusresource.setStatus(0xFF, "Saved"); // Set operation in progress
}

