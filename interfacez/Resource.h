#ifndef __RESOURCE_H__
#define __RESOURCE_H__

#include <QQueue>

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

#endif
