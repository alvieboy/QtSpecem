#include "SocketClient.h"
#include "interfacez.h"
#include <QAbstractSocket>
#include <QTcpSocket>

SocketClient::SocketClient(InterfaceZ*me, QAbstractSocket *s): Client(me), m_sock(s)
{

    hdlc_encoder__init(&m_hdlc_encoder, &hdlc_writer, &hdlc_flusher, this);

    connect(m_sock, &QTcpSocket::readyRead, this, &SocketClient::readyRead);

    connect(m_sock, qOverload<QAbstractSocket::SocketError>(&QAbstractSocket::error),
            this, &SocketClient::socketError);


    hdlc_decoder__init(&m_hdlc_decoder,
                       m_hdlcrxbuf,
                       sizeof(m_hdlcrxbuf),
                       &SocketClient::hdlcDataReady,
                       NULL,
                       this);
}

void SocketClient::hdlc_writer(void *userdata, const uint8_t ch)
{
    SocketClient *c = static_cast<SocketClient*>(userdata);

    c->m_txarray.append((char)ch);
}

void SocketClient::hdlc_flusher(void *userdata)
{
    SocketClient *c = static_cast<SocketClient*>(userdata);

    if (c->m_sock->write(c->m_txarray)<0)
    {
    }
        //abort();
    c->m_txarray.clear();
}

void SocketClient::hdlcDataReady(void *user, const uint8_t *data, unsigned len)
{
    SocketClient *c = static_cast<SocketClient*>(user);

    //c->intf->hdlcDataReady(c, data,len);
    c->handleHDLC(data,len);
}

void SocketClient::socketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    interfacez_debug("Socket error %s",getError().toLocal8Bit().constData());
    close();
    intf->removeClient(this);
}

void SocketClient::readyRead()
{
    uint8_t rxbuf[256];

    int len;

    do {
        len = m_sock->read( (char*)rxbuf, sizeof(rxbuf));

        if (len<0) {
            return;
        }

        hdlc_decoder__append_buffer(&m_hdlc_decoder, rxbuf, len);

    } while (len);
}

void SocketClient::handleHDLC(const uint8_t *data, unsigned datalen)
{

    uint8_t *txbuf_complete = (uint8_t*)malloc(datalen+ 8);
    txbuf_complete[0] = data[0];

    transceive(data, &txbuf_complete[1], datalen);

    hdlc_encoder__begin(&m_hdlc_encoder);

    uint8_t scmd = 0x01;

    hdlc_encoder__write(&m_hdlc_encoder, &scmd, sizeof(scmd));
    hdlc_encoder__write(&m_hdlc_encoder, txbuf_complete, datalen+1);
    hdlc_encoder__end(&m_hdlc_encoder);

    free(txbuf_complete);
}

void SocketClient::gpioEvent(uint8_t v)
{
    hdlc_encoder__begin(&m_hdlc_encoder);
    uint8_t cmd[2]= { 0x00, v };
    hdlc_encoder__write(&m_hdlc_encoder, &cmd, sizeof(cmd));
    hdlc_encoder__end(&m_hdlc_encoder);
}

void SocketClient::connectUSB(const char *id)
{
    uint8_t len = strlen(id);
    hdlc_encoder__begin(&m_hdlc_encoder);

    uint8_t cmd[2]= { 0x03, len };
    hdlc_encoder__write(&m_hdlc_encoder, &cmd, sizeof(cmd));
    hdlc_encoder__write(&m_hdlc_encoder, id, len);
    hdlc_encoder__end(&m_hdlc_encoder);

};

void SocketClient::sendGPIOupdate(uint64_t v)
{
    interfacez_debug("Sending update GPIO");
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
            interfacez_debug("0x%02x ", cmd[i]);
        }
        interfacez_debug("");
    }
    hdlc_encoder__write(&m_hdlc_encoder, &cmd, sizeof(cmd));
    hdlc_encoder__end(&m_hdlc_encoder);
}


void SocketClient::close()
{

}
