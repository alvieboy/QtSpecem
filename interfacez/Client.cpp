#include "Client.h"
#include "interfacez.h"

void Client::transceive(const uint8_t *data, uint8_t *rx, unsigned len)
{
    intf->transceive(this, data, rx, len);
}
