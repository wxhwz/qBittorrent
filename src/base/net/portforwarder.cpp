#include "portforwarder.h"

Net::PortForwarder::PortForwarder(QObject *parent)
    : QObject {parent}
{
    Q_ASSERT(!m_instance);
    m_instance = this;
}

Net::PortForwarder::~PortForwarder()
{
    m_instance = nullptr;
}

Net::PortForwarder *Net::PortForwarder::instance()
{
    return m_instance;
}

Net::PortForwarder *Net::PortForwarder::m_instance = nullptr;
