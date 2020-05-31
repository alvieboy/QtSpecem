#ifndef __INT8RESOURCE_H__
#define __INT8RESOURCE_H__

#include "interfacez/Resource.h"

struct Int8Resource: public Resource
{
    Int8Resource(const uint8_t *val): m_ptr(val) {
    }
    uint8_t type() const override { return 0; }
    uint16_t len() const override { return 1; }
    void copyTo(QQueue<uint8_t> &queue) const override{
        queue.push_back(*m_ptr);
    }
private:
    const uint8_t *m_ptr;
};

#endif
