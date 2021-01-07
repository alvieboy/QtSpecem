#ifndef __CLIENT_H__
#define __CLIENT_H__

#include <QObject>

class InterfaceZ;

struct Client: public QObject
{
    Q_OBJECT
    public:
        Client(InterfaceZ*me): intf(me) {
        }
        virtual ~Client() {}
        virtual void gpioEvent(uint8_t) =0;
        virtual void sendGPIOupdate(uint64_t) = 0;
        virtual void transceive(const uint8_t *in, uint8_t *out, unsigned len);
        virtual QString getError() = 0;
        virtual void connectUSB(const char *id) = 0;
        virtual void close() = 0;
        InterfaceZ *intf;
};

#endif
