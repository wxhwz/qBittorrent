#pragma once

#include <libtorrent/config.hpp>

#include <QtSystemDetection>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>

#include "guiapplicationcomponent.h"

class AdvancedSettings final : public GUIApplicationComponent<QTableWidget>
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AdvancedSettings)

public:
    explicit AdvancedSettings(IGUIApplication *app, QWidget *parent = nullptr);

public slots:
    void saveAdvancedSettings() const;

signals:
    void settingsChanged();

private slots:
    void updateInterfaceAddressCombo();

#ifndef QBT_USES_LIBTORRENT2
    void updateCacheSpinSuffix(int value);
#endif

#ifdef QBT_USES_DBUS
    void updateNotificationTimeoutSuffix(int value);
#endif

private:
    void loadAdvancedSettings();
    template <typename T> void addRow(int row, const QString &text, T *widget);

    QSpinBox m_spinBoxSaveResumeDataInterval, m_spinBoxSaveStatisticsInterval, m_spinBoxTorrentFileSizeLimit, m_spinBoxBdecodeDepthLimit, m_spinBoxBdecodeTokenLimit,
             m_spinBoxAsyncIOThreads, m_spinBoxFilePoolSize, m_spinBoxCheckingMemUsage, m_spinBoxDiskQueueSize,
             m_spinBoxOutgoingPortsMin, m_spinBoxOutgoingPortsMax, m_spinBoxUPnPLeaseDuration, m_spinBoxPeerDSCP, m_spinBoxHostnameCacheTTL,
             m_spinBoxListRefresh, m_spinBoxTrackerPort, m_spinBoxSendBufferWatermark, m_spinBoxSendBufferLowWatermark,
             m_spinBoxSendBufferWatermarkFactor, m_spinBoxConnectionSpeed, m_spinBoxSocketSendBufferSize, m_spinBoxSocketReceiveBufferSize, m_spinBoxSocketBacklogSize,
             m_spinBoxAnnouncePort, m_spinBoxMaxConcurrentHTTPAnnounces, m_spinBoxStopTrackerTimeout, m_spinBoxSessionShutdownTimeout,
             m_spinBoxSavePathHistoryLength, m_spinBoxPeerTurnover, m_spinBoxPeerTurnoverCutoff, m_spinBoxPeerTurnoverInterval, m_spinBoxRequestQueueSize;
    QCheckBox m_checkBoxOsCache, m_checkBoxRecheckCompleted, m_checkBoxResolveCountries, m_checkBoxResolveHosts,
              m_checkBoxProgramNotifications, m_checkBoxTorrentAddedNotifications, m_checkBoxReannounceWhenAddressChanged, m_checkBoxTrackerFavicon, m_checkBoxTrackerStatus,
              m_checkBoxTrackerPortForwarding, m_checkBoxIgnoreSSLErrors, m_checkBoxConfirmTorrentRecheck, m_checkBoxConfirmRemoveAllTags, m_checkBoxAnnounceAllTrackers,
              m_checkBoxAnnounceAllTiers, m_checkBoxMultiConnectionsPerIp, m_checkBoxValidateHTTPSTrackerCertificate, m_checkBoxSSRFMitigation, m_checkBoxBlockPeersOnPrivilegedPorts,
              m_checkBoxPieceExtentAffinity, m_checkBoxSuggestMode, m_checkBoxSpeedWidgetEnabled, m_checkBoxIDNSupport, m_checkBoxConfirmRemoveTrackerFromAllTorrents,
              m_checkBoxStartSessionPaused;
    QComboBox m_comboBoxInterface, m_comboBoxInterfaceAddress, m_comboBoxDiskIOReadMode, m_comboBoxDiskIOWriteMode, m_comboBoxUtpMixedMode, m_comboBoxChokingAlgorithm,
              m_comboBoxSeedChokingAlgorithm, m_comboBoxResumeDataStorage, m_comboBoxTorrentContentRemoveOption;
    QLineEdit m_lineEditAppInstanceName, m_pythonExecutablePath, m_lineEditAnnounceIP, m_lineEditDHTBootstrapNodes;

    //custom
    QLineEdit m_lineEditCustomPeerID, m_lineEditCustomUserAgent;

    QRegularExpressionValidator* m_lineEditCustomPeerIDValidator = nullptr;

#ifndef QBT_USES_LIBTORRENT2
    QSpinBox m_spinBoxCache, m_spinBoxCacheTTL;
    QCheckBox m_checkBoxCoalesceRW;
#else
    QComboBox m_comboBoxDiskIOType;
    QSpinBox m_spinBoxHashingThreads;
#endif

#if defined(QBT_USES_LIBTORRENT2) && !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS)
    QSpinBox m_spinBoxMemoryWorkingSetLimit;
#endif

#if defined(QBT_USES_LIBTORRENT2) && TORRENT_USE_I2P
    QSpinBox m_spinBoxI2PInboundQuantity, m_spinBoxI2POutboundQuantity, m_spinBoxI2PInboundLength, m_spinBoxI2POutboundLength;
#endif

    // OS dependent settings
#ifdef Q_OS_WIN
    QComboBox m_comboBoxOSMemoryPriority;
#endif

#ifndef Q_OS_MACOS
    QCheckBox m_checkBoxIconsInMenusEnabled;
    QCheckBox m_checkBoxAttachedAddNewTorrentDialog;
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QCheckBox m_checkBoxMarkOfTheWeb;
#endif // Q_OS_MACOS || Q_OS_WIN

#ifdef QBT_USES_DBUS
    QSpinBox m_spinBoxNotificationTimeout;
#endif
};
