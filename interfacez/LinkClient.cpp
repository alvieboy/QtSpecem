#include "LinkClient.h"
#include "../interface.h"
/*
 void hdlc_data(void *user, const uint8_t *data, unsigned datalen)
{
    struct spi_payload payload;
#if 0
    dump("SPI IN (via hdlc): ",data, datalen);
#endif
    switch (data[0]) {
    case 0x00:
        // Interrupt (pin) data
        //printf("**** Interrupt **** source=%d\n", data[1]);
        gpio_isr_do_trigger(data[1]);

        break;
    case 0x01:
        // Spi data
        payload.data = malloc(datalen-1);
        memcpy(payload.data, &data[1], datalen-1);
        payload.len = datalen - 1;

        queue__send_from_isr(fpga_spi_queue, &payload, NULL);

        break;
    case 0x02:
        // Raw pin data
        pinstate =
            ((uint64_t)data[1] << 56) |
            ((uint64_t)data[2] << 48) |
            ((uint64_t)data[3] << 40) |
            ((uint64_t)data[4] << 32) |
            ((uint64_t)data[5] << 24) |
            ((uint64_t)data[6] << 16) |
            ((uint64_t)data[7] << 8) |
            ((uint64_t)data[8] << 0);
        printf("**** GPIO update %016lx ****\n", pinstate);

        break;
    case 0x03:
        printf("USB connection\n");
        uint8_t len = data[1];
        char *newid = malloc(len);
        strncpy(newid, (char*)&data[2], len);
        interface__connectusb(newid);
        break;
    }
}
*/
LinkClient::LinkClient(InterfaceZ *me): Client(me) {

}


void LinkClient::gpioEvent(uint8_t v)
{
    interface__gpio_trigger(v);
}

void LinkClient::sendGPIOupdate(uint64_t v)
{
    interface__rawpindata(v);

}

QString LinkClient::getError()
{
}

void LinkClient::connectUSB(const char *id)
{
    interface__connectusb(id);

}

void LinkClient::close()
{
}

void LinkClient::transceive(const uint8_t *in, uint8_t *out, unsigned len)
{
    Client::transceive(in, out, len);
}
