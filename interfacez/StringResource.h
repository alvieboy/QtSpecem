#ifndef __STRINGRESOURCE_H__
#define __STRINGRESOURCE_H__

#include "interfacez/Resource.h"
#include <QString>

struct StringResource: public Resource
{
    StringResource(QString &s): m_str(s){
    }
    uint8_t type() const override { return 0x01; }
    uint16_t len() const override { return m_str.length() + 1; }

    void copyTo(QQueue<uint8_t> &queue) const override{
        queue.push_back(m_str.length() & 0xff);
        for (auto i: m_str) {
            queue.push_back(i.toLatin1());
        }
    }
private:
    QString &m_str;
};

#endif
