#include "Client.h"

struct LinkClient: public Client
{
    LinkClient(InterfaceZ*me);
    virtual ~LinkClient() {}
    virtual void gpioEvent(uint8_t) override;
    virtual void sendGPIOupdate(uint64_t) override;
    virtual QString getError() override;
    virtual void connectUSB(const char *id);
    virtual void close() override;
    virtual void transceive(const uint8_t *in, uint8_t *out, unsigned len);
};
