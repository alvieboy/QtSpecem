#include "interfacez.h"
#include <QDebug>
#include <QHostAddress>
#include <QTcpSocket>
#include <QObject>
#include <QAbstractSocket>
#include <QFile>
#include <QByteArray>
#include "sna_relocs.h"

extern "C" {
#include "z80core/iglobal.h"

    void execute_if_running()
    {
        execute();
    }

    void retn_called_hook()
    {
        // Upon retn, restore ROM.
        printf("RETN called, restoring stock ROM\n");
        set_current_rom(NULL);
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

    cmdt = new command_t; // THIS MUST BE by socket

    m_cmdsocket = new QTcpServer();

    connect(m_cmdsocket, &QTcpServer::newConnection, this, &InterfaceZ::newConnection);



    if (!m_cmdsocket->listen(QHostAddress::Any, 8003)) { // QTcpSocket::ReuseAddressHint
        qDebug()<<"Cannot listen";
        return -1;
    }

    m_sna_rom_size = -1;

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
    case 0x0B:
        //qDebug()<<"Head "<<(void*)m_sna_head<<"tail"<<(void*)m_sna_tail<<"size"<<m_sna_size <<"Diff"<<(m_sna_tail-m_sna);
        if ( m_sna_head == m_sna_tail ) {
            val = 0x01;
          break;
        }
        val = 0x00;
        break;

    case 0x0D:
        if ( m_sna_head == m_sna_tail ) {
            val = 0x00;
            break;
        }
        val = *m_sna_head++;
        break;
    }

    //printf("InterfaceZ IOREAD 0x%02x: 0x%02x\n",  address, val);
    return val;
}

void InterfaceZ::iowrite(UCHAR address, UCHAR value)
{
    qDebug()<<"InterfaceZ IOWRITE "<<(int)address<<"value"<<(int)value;
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
        QFile file(":/rom/sna_loader.rom");

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
    apply_relocs(m_sna, &m_sna_rom[0]);
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

    if (remain==0)
        return 0;

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
    }

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
