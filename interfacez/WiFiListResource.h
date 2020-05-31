#include "interfacez/Resource.h"
#include <QString>

struct WiFiListResource: public Resource
{
    struct AccessPoint
    {
        AccessPoint(uint8_t f, const QString &ss): flags(f), ssid(ss) {}
        uint8_t flags;
        QString ssid;
    };

    WiFiListResource(QList<AccessPoint>&aplist): m_accesspoints(aplist) {}
    uint8_t type() const override { return RESOURCE_TYPE_APLIST; }
    uint16_t len() const override;
    void copyTo(QQueue<uint8_t> &queue) const override;
private:
    QList<AccessPoint> &m_accesspoints;
};
