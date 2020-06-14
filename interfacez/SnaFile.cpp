#include "SnaFile.h"

int SnaFile::open()
{
    if (!m_file.open(QIODevice::WriteOnly|QIODevice::Truncate)){
        return -1;
    }
    return 0;
}
void SnaFile::write(const uint8_t val)
{
    m_file.write((const char*)&val,1);
}
void SnaFile::write(const uint8_t *buf, size_t len)
{
    m_file.write((const char*)buf, len);
}
void SnaFile::close()
{
    m_file.close();
}
