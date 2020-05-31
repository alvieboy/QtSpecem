#include "interfacez/Resource.h"

struct OpStatusResource: public Resource
{
    OpStatusResource(): m_val(0xff) {
    }
    uint8_t type() const override { return 0x05; }
    uint16_t len() const override { return m_str.length() + 2; }

    void copyTo(QQueue<uint8_t> &queue) const override{
        printf("Reading status: %d\n", m_val);
        queue.push_back(m_val);
        queue.push_back(m_str.length() & 0xff);
        for (auto i: m_str) {
            queue.push_back(i.toLatin1());
        }
    }
    void setStatus(uint8_t val, const QString &msg){
        m_val=val;
        m_str = msg;
    }
private:
    uint8_t m_val;
    QString m_str;
};
