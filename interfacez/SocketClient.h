#ifndef __SOCKETCLIENT_H__
#define __SOCKETCLIENT_H__

#include "Client.h"
#include "hdlc_decoder.h"
#include "hdlc_encoder.h"
#include <QAbstractSocket>

class QByteArray;

struct SocketClient: public Client
{
    SocketClient(InterfaceZ*me, QAbstractSocket *s);
    virtual ~SocketClient() {}
    static void hdlc_writer(void *userdata, const uint8_t ch);
    static void hdlc_flusher(void *userdata);

    virtual void gpioEvent(uint8_t) override;
    virtual void sendGPIOupdate(uint64_t) override;
    virtual void connectUSB(const char *id) override;
    static void hdlcDataReady(void *user, const uint8_t *data, unsigned len);
    void handleHDLC(const uint8_t *data, unsigned len);

    virtual QString getError() override {return m_sock->errorString(); }

    void readyRead();
    void socketError(QAbstractSocket::SocketError);

    void close() override;

    QAbstractSocket *m_sock;
    uint8_t m_hdlcrxbuf[8192];
    hdlc_decoder_t m_hdlc_decoder;
    hdlc_encoder_t m_hdlc_encoder;
    QByteArray m_txarray;
};

#endif
