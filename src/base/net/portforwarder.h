#pragma once

#include <QObject>
#include <QSet>

class QString;

namespace Net
{
    class PortForwarder : public QObject
    {
        Q_DISABLE_COPY_MOVE(PortForwarder)

    public:
        explicit PortForwarder(QObject *parent = nullptr);
        ~PortForwarder() override;

        static PortForwarder *instance();

        virtual bool isEnabled() const = 0;
        virtual void setEnabled(bool enabled) = 0;

        virtual void setPorts(const QString &profile, QSet<quint16> ports) = 0;
        virtual void removePorts(const QString &profile) = 0;

    private:
        static PortForwarder *m_instance;
    };
}
