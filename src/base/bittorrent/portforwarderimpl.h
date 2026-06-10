#pragma once

#include <QHash>
#include <QSet>

#include "base/net/portforwarder.h"
#include "base/settingvalue.h"

namespace BitTorrent
{
    class SessionImpl;
}

class PortForwarderImpl final : public Net::PortForwarder
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PortForwarderImpl)

public:
    explicit PortForwarderImpl(BitTorrent::SessionImpl *provider, QObject *parent = nullptr);
    ~PortForwarderImpl() override;

    bool isEnabled() const override;
    void setEnabled(bool enabled) override;

    void setPorts(const QString &profile, QSet<quint16> ports) override;
    void removePorts(const QString &profile) override;

private:
    void start();
    void stop();

    CachedSettingValue<bool> m_storeActive;

    BitTorrent::SessionImpl *const m_provider = nullptr;
    QHash<QString, QSet<quint16>> m_portProfiles;
};
