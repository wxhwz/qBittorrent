#include "portforwarderimpl.h"

#include <utility>

#include "base/bittorrent/sessionimpl.h"

PortForwarderImpl::PortForwarderImpl(BitTorrent::SessionImpl *provider, QObject *parent)
    : Net::PortForwarder(parent)
    , m_storeActive {u"Network/PortForwardingEnabled"_s, true}
    , m_provider {provider}
{
    if (isEnabled())
        start();
}

PortForwarderImpl::~PortForwarderImpl()
{
    stop();
}

bool PortForwarderImpl::isEnabled() const
{
    return m_storeActive;
}

void PortForwarderImpl::setEnabled(const bool enabled)
{
    if (m_storeActive == enabled)
        return;

    if (enabled)
        start();
    else
        stop();
    m_storeActive = enabled;
}

void PortForwarderImpl::setPorts(const QString &profile, QSet<quint16> ports)
{
    const QSet<quint16> oldForwardedPorts = std::accumulate(m_portProfiles.cbegin(), m_portProfiles.cend(), QSet<quint16>());

    m_portProfiles[profile] = std::move(ports);
    const QSet<quint16> newForwardedPorts = std::accumulate(m_portProfiles.cbegin(), m_portProfiles.cend(), QSet<quint16>());

    m_provider->removeMappedPorts(oldForwardedPorts - newForwardedPorts);
    m_provider->addMappedPorts(newForwardedPorts - oldForwardedPorts);
}

void PortForwarderImpl::removePorts(const QString &profile)
{
    setPorts(profile, {});
}

void PortForwarderImpl::start()
{
    m_provider->enablePortMapping();
    for (const QSet<quint16> &ports : asConst(m_portProfiles))
        m_provider->addMappedPorts(ports);
}

void PortForwarderImpl::stop()
{
    m_provider->disablePortMapping();
}
