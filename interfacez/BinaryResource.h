#ifndef __BINARYRESOURCE_H__
#define __BINARYRESOURCE_H__

#include "interfacez/Resource.h"
#include <QByteArray>

struct BinaryResource: public Resource
{
    BinaryResource(const uint8_t type, const uint8_t *data, unsigned len): m_type(type), m_data((const char*)data,len) {
    }
    BinaryResource(const uint8_t type, const QByteArray &data): m_type(type), m_data(data) {
    }
    uint8_t type() const override { return m_type; }
    uint16_t len() const override { return m_data.length(); }
    void copyTo(QQueue<uint8_t> &queue) const override{
        for (auto i: m_data) {
            queue.push_back(i);
        }
    }
private:
    uint8_t m_type;
    QByteArray m_data;
};

#endif
