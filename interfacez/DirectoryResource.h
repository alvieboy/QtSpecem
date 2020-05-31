#include "interfacez/Resource.h"

struct DirectoryResource: public Resource
{
    DirectoryResource();
    uint8_t type() const override;
    uint16_t len() const override;
    void copyTo(QQueue<uint8_t> &queue) const override;

#define FLAG_FILE 0
#define FLAG_DIRECTORY 1

    struct fileentry {
        fileentry(uint8_t f, const QString &n): flags(f), name(n) {
        }
        uint8_t flags;
        QString name;
    };
    QList<struct fileentry> m_files;
    QString m_cwd;
};
