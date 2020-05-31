#include "interfacez/Resource.h"

struct StatusResource: public Resource
{
    StatusResource(): m_val(0) {
    }
    uint8_t type() const override { return RESOURCE_TYPE_INTEGER; }
    uint16_t len() const override { return 1; }
    void set(uint8_t val) {
        m_val = val;
    }
    void setbit(uint8_t bit)
    {
        m_val = m_val | (1<<bit);
    }
    void clearbit(uint8_t bit)
    {
        m_val = m_val & ~(1<<bit);
    }

    uint8_t get() const {
        return m_val;
    }
    void copyTo(QQueue<uint8_t> &queue) const override{
        qDebug()<<"Sta "<<m_val;
        queue.push_back( m_val );
    }
private:
    uint8_t m_val;
};
