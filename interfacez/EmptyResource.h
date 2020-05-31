#ifndef __EMPTYRESOURCE_H__
#define __EMPTYRESOURCE_H__

#include "interfacez/Resource.h"

struct EmptyResource: public Resource
{
    EmptyResource(const uint8_t type): m_type(type) {
    }
    uint8_t type() const override { return m_type; }
    uint16_t len() const override { return 0; }
    void copyTo(QQueue<uint8_t> &queue) const override{
        Q_UNUSED(queue)
    }
private:
    uint8_t m_type;
};

#endif
