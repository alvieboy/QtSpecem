#include "Client.h"

struct LinkClient: public Client
{
    LinkClient(InterfaceZ*me);
    virtual ~LinkClient() {}
    virtual void gpioEvent(uint8_t) override = 0;
    virtual void sendGPIOupdate(uint64_t) override = 0;
    virtual QString getError() override = 0;
    virtual void close() override = 0;
};
