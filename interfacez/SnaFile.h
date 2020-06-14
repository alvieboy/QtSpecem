#ifndef __SNAFILE_H__
#define __SNAFILE_H__

#include <QFile>

class SnaFile
{
public:
    SnaFile(const QString &n): m_file(n)
    {
    }
    int open();
    void write(const uint8_t val);
    void write(const uint8_t *buf, size_t len);
    void close();
private:
    QFile m_file;
};

#endif
