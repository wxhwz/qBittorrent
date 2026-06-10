#include "statusbar.h"

#include <QApplication>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>

//my
#include <QMenu>
#include <QAction>
#include <QDesktopServices>
#include <QUrl>


#include "base/bittorrent/session.h"
#include "base/bittorrent/sessionstatus.h"
#include "base/preferences.h"
#include "base/utils/misc.h"
#include "speedlimitdialog.h"
#include "uithememanager.h"
#include "utils.h"

namespace
{
    QWidget* createSeparator(QWidget* parent)
    {
        QFrame* separator = new QFrame(parent);
        separator->setFrameStyle(QFrame::VLine);
#ifndef Q_OS_MACOS
        separator->setFrameShadow(QFrame::Raised);
#endif
        return separator;
    }
}

// 状态栏构造函数，初始化所有控件和信号槽连接
StatusBar::StatusBar(QWidget* parent)
    : QStatusBar(parent)  // 调用基类构造函数
{
#ifndef Q_OS_MACOS
    // 在 macOS 上，重新定义全局样式表会破坏某些元素（如标签页）。
    // Qt 会检查样式表类是否继承自 "QMacStyle"，重新定义后该检查会失败。
    // 因此非 macOS 平台才设置此样式，去除状态栏子项之间的边框。
    setStyleSheet(u"QStatusBar::item { border-width: 0; }"_s);
#endif

    // 获取 BitTorrent 会话单例对象
    BitTorrent::Session* const session = BitTorrent::Session::instance();
    // 当速度限制模式改变时，更新备用速度按钮的显示
    connect(session, &BitTorrent::Session::speedLimitModeChanged, this, &StatusBar::updateAltSpeedsBtn);

    // 创建一个容器 Widget，用于容纳状态栏中的所有控件
    QWidget* container = new QWidget(this);
    // 水平布局，让控件从左到右排列
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);  // 移除布局外边距

    container->setLayout(layout);

    // ---------- 连接状态按钮（显示防火墙/连接图标） ----------
    m_connecStatusLblIcon = new QPushButton(this);
    m_connecStatusLblIcon->setFlat(true);                 // 扁平风格，无边框
    m_connecStatusLblIcon->setFocusPolicy(Qt::NoFocus);   // 不获取焦点
    m_connecStatusLblIcon->setCursor(Qt::PointingHandCursor); // 手型光标，提示可点击
    m_connecStatusLblIcon->setIcon(UIThemeManager::instance()->getIcon(u"firewalled"_s)); // 初始图标为“防火墙”
    // 设置工具提示，显示连接状态说明（无直接连接可能表示网络配置问题）
    m_connecStatusLblIcon->setToolTip(u"<b>%1</b><br><i>%2</i>"_s.arg(tr("Connection status:")
        , tr("No direct connections. This may indicate network configuration problems.")));
    // 点击按钮时发射 connectionButtonClicked 信号
    connect(m_connecStatusLblIcon, &QAbstractButton::clicked, this, &StatusBar::connectionButtonClicked);

    // ---------- 下载速度显示按钮 ----------
    m_dlSpeedLbl = new QPushButton(this);
    m_dlSpeedLbl->setIcon(UIThemeManager::instance()->getIcon(u"downloading"_s, u"downloading_small"_s));
    // 点击按钮时调用 capSpeed() 槽函数（通常用于打开速度限制设置）
    connect(m_dlSpeedLbl, &QAbstractButton::clicked, this, &StatusBar::capSpeed);
    m_dlSpeedLbl->setFlat(true);
    m_dlSpeedLbl->setFocusPolicy(Qt::NoFocus);
    m_dlSpeedLbl->setCursor(Qt::PointingHandCursor);
    m_dlSpeedLbl->setStyleSheet(u"text-align:left;"_s);  // 文字左对齐
    m_dlSpeedLbl->setMinimumWidth(200);                  // 最小宽度，保证布局稳定

    // ---------- 上传速度显示按钮 ----------
    m_upSpeedLbl = new QPushButton(this);
    m_upSpeedLbl->setIcon(UIThemeManager::instance()->getIcon(u"upload"_s, u"seeding"_s));
    connect(m_upSpeedLbl, &QAbstractButton::clicked, this, &StatusBar::capSpeed);
    m_upSpeedLbl->setFlat(true);
    m_upSpeedLbl->setFocusPolicy(Qt::NoFocus);
    m_upSpeedLbl->setCursor(Qt::PointingHandCursor);
    m_upSpeedLbl->setStyleSheet(u"text-align:left;"_s);
    m_upSpeedLbl->setMinimumWidth(200);

    // ---------- 可用磁盘空间标签 ----------
    m_freeDiskSpaceLbl = new QLabel(tr("Free space: N/A")); // 初始显示不可用
    m_freeDiskSpaceLbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    // 创建分隔线（与标签关联的垂直分隔条）
    m_freeDiskSpaceSeparator = createSeparator(m_freeDiskSpaceLbl);

    // ---------- 外部 IP 地址标签 ----------
    m_lastExternalIPsLbl = new QLabel(tr("External IP: N/A"));
    m_lastExternalIPsLbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    m_lastExternalIPsSeparator = createSeparator(m_lastExternalIPsLbl);

    // 【新增代码开始】
    // 允许该标签接收自定义右键菜单事件
    m_lastExternalIPsLbl->setContextMenuPolicy(Qt::CustomContextMenu);
    // 将右键请求信号连接到我们声明的槽函数上
    connect(m_lastExternalIPsLbl, &QLabel::customContextMenuRequested, this, &StatusBar::showExternalIpMenu);
    m_lastExternalIPsLbl->setToolTip(tr("Right-click to test IPv4 or IPv6 connectivity"));
    // 【新增代码结束】

    // ---------- DHT 节点数量标签 ----------
    m_DHTLbl = new QLabel(tr("DHT: %1 nodes").arg(0), this);
    m_DHTLbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    m_DHTSeparator = createSeparator(m_DHTLbl);

    // ---------- 备用速度限制切换按钮 ----------
    m_altSpeedsBtn = new QPushButton(this);
    m_altSpeedsBtn->setFlat(true);
    m_altSpeedsBtn->setFocusPolicy(Qt::NoFocus);
    m_altSpeedsBtn->setCursor(Qt::PointingHandCursor);
    // 根据当前是否启用备用速度限制来更新按钮的图标和文字
    updateAltSpeedsBtn(session->isAltGlobalSpeedLimitEnabled());
    connect(m_altSpeedsBtn, &QAbstractButton::clicked, this, &StatusBar::alternativeSpeedsButtonClicked);

    // ---------- 图标尺寸调整 ----------
    // 某些平台上默认图标较大，会导致状态栏过高（即使图标实际只有 16x16）
    m_connecStatusLblIcon->setIconSize(Utils::Gui::smallIconSize());   // 小图标尺寸
    m_dlSpeedLbl->setIconSize(Utils::Gui::smallIconSize());
    m_upSpeedLbl->setIconSize(Utils::Gui::smallIconSize());
    // 备用速度按钮的图标高度用小图标，宽度用中等图标尺寸
    m_altSpeedsBtn->setIconSize(QSize(Utils::Gui::mediumIconSize().width(), Utils::Gui::smallIconSize().height()));

    // 设置已知的最大宽度（加一些内边距），让速度控件占据剩余空间
    m_connecStatusLblIcon->setMaximumWidth(Utils::Gui::largeIconSize().width());
    m_altSpeedsBtn->setMaximumWidth(Utils::Gui::largeIconSize().width());

    // ---------- 将所有控件添加到布局中 ----------
    layout->addWidget(m_freeDiskSpaceLbl);
    layout->addWidget(m_freeDiskSpaceSeparator);

    layout->addWidget(m_lastExternalIPsLbl);
    layout->addWidget(m_lastExternalIPsSeparator);

    layout->addWidget(m_DHTLbl);
    layout->addWidget(m_DHTSeparator);

    layout->addWidget(m_connecStatusLblIcon);
    layout->addWidget(createSeparator(m_connecStatusLblIcon));   // 连接状态按钮后面的分隔线

    layout->addWidget(m_altSpeedsBtn);
    layout->addWidget(createSeparator(m_altSpeedsBtn));

    layout->addWidget(m_dlSpeedLbl);
    layout->addWidget(createSeparator(m_dlSpeedLbl));

    layout->addWidget(m_upSpeedLbl);  // 最后一个控件后面不加分隔线

    addPermanentWidget(container);               // 将整个容器添加到状态栏的永久区域（右侧）
    setStyleSheet(u"QWidget {margin: 0;}"_s);      // 移除所有 Widget 的外边距
    container->adjustSize();                     // 根据内容调整容器大小
    adjustSize();                                // 调整状态栏自身大小

    // 根据用户设置显示或隐藏可用磁盘空间和外部 IP 标签
    updateFreeDiskSpaceVisibility();
    updateExternalAddressesVisibility();

    // 检查 DHT 是否启用
    const bool isDHTVisible = session->isDHTEnabled();
    m_DHTLbl->setVisible(isDHTVisible);
    m_DHTSeparator->setVisible(isDHTVisible);

    refresh();  // 立即刷新一次，显示当前速度、连接状态等

    // 当会话统计数据更新时，刷新状态栏
    connect(session, &BitTorrent::Session::statsUpdated, this, &StatusBar::refresh);

    // 获取当前可用磁盘空间并更新标签
    updateFreeDiskSpaceLabel(session->freeDiskSpace());
    connect(session, &BitTorrent::Session::freeDiskSpaceChecked, this, &StatusBar::updateFreeDiskSpaceLabel);

    // 监听配置变化，当用户保存选项时，重新调整状态栏控件的可见性等
    connect(Preferences::instance(), &Preferences::changed, this, &StatusBar::optionsSaved);
}

// 析构函数，仅用于调试输出
StatusBar::~StatusBar()
{
    qDebug() << Q_FUNC_INFO;  // 打印函数名，用于追踪对象销毁
}

// 显示需要重启 qBittorrent 的提示（例如更新设置后需要重启才能生效）
void StatusBar::showRestartRequired()
{
    // 重启提示文本
    const QString restartText = tr("qBittorrent needs to be restarted!");

    // 获取系统标准的警告图标（小尺寸）
    const QPixmap pixmap = style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(Utils::Gui::smallIconSize());
    auto* restartIconLbl = new QLabel(this);
    restartIconLbl->setPixmap(pixmap);
    restartIconLbl->setToolTip(restartText);  // 鼠标悬停时显示提示文本
    insertWidget(0, restartIconLbl);          // 插入到状态栏最左侧（索引0）

    auto* restartLbl = new QLabel(this);
    restartLbl->setText(restartText);
    insertWidget(1, restartLbl);              // 在图标右侧插入文字标签
}
// 更新连接状态（根据监听状态和传入连接情况显示不同图标和提示）
void StatusBar::updateConnectionStatus()
{
    // 获取当前 BitTorrent 会话的状态信息
    const BitTorrent::SessionStatus& sessionStatus = BitTorrent::Session::instance()->status();

    // 如果会话未在监听端口（例如端口被占用或未启动）
    if (!BitTorrent::Session::instance()->isListening())
    {
        // 显示“断开连接”图标
        m_connecStatusLblIcon->setIcon(UIThemeManager::instance()->getIcon(u"disconnected"_s));
        // 提示：离线状态，通常意味着 qBittorrent 无法在选定的端口上监听传入连接
        const QString tooltip = u"<b>%1</b><br>%2"_s.arg(tr("Connection Status:"), tr("Offline. This usually means that qBittorrent failed to listen on the selected port for incoming connections."));
        m_connecStatusLblIcon->setToolTip(tooltip);
    }
    else  // 正在监听端口
    {
        if (sessionStatus.hasIncomingConnections)  // 有传入连接，连接正常
        {
            // 显示“已连接”图标
            m_connecStatusLblIcon->setIcon(UIThemeManager::instance()->getIcon(u"connected"_s));
            const QString tooltip = u"<b>%1</b><br>%2"_s.arg(tr("Connection Status:"), tr("Online"));
            m_connecStatusLblIcon->setToolTip(tooltip);
        }
        else  // 监听中但没有传入连接，可能是防火墙或 NAT 问题
        {
            // 显示“防火墙”图标（警告状态）
            m_connecStatusLblIcon->setIcon(UIThemeManager::instance()->getIcon(u"firewalled"_s));
            const QString tooltip = u"<b>%1</b><br><i>%2</i>"_s.arg(tr("Connection Status:"), tr("No direct connections. This may indicate network configuration problems."));
            m_connecStatusLblIcon->setToolTip(tooltip);
        }
    }
}

// 更新 DHT 节点数量的显示
void StatusBar::updateDHTNodesNumber()
{
    // 检查 DHT 功能是否已启用
    if (BitTorrent::Session::instance()->isDHTEnabled())
    {
        // 启用则显示标签和分隔线
        m_DHTLbl->setVisible(true);
        m_DHTSeparator->setVisible(true);
        // 设置文本，显示当前 DHT 节点数
        m_DHTLbl->setText(tr("DHT: %1 nodes").arg(BitTorrent::Session::instance()->status().dhtNodes));
    }
    else
    {
        // 未启用则隐藏标签和分隔线
        m_DHTLbl->setVisible(false);
        m_DHTSeparator->setVisible(false);
    }
}

// 更新可用磁盘空间标签（参数 value 为字节数）
void StatusBar::updateFreeDiskSpaceLabel(const qint64 value)
{
    // 将字节数转换为友好的单位（如 KB, MB, GB）并设置文本
    m_freeDiskSpaceLbl->setText(tr("Free space: ") + Utils::Misc::friendlyUnit(value));
}

// 根据用户设置显示或隐藏可用磁盘空间标签及其分隔线
void StatusBar::updateFreeDiskSpaceVisibility()
{
    const bool isVisible = Preferences::instance()->isStatusbarFreeDiskSpaceDisplayed();
    m_freeDiskSpaceLbl->setVisible(isVisible);
    m_freeDiskSpaceSeparator->setVisible(isVisible);
}

// 更新外部 IP 地址标签（显示 IPv4 和 IPv6 地址及端口）
void StatusBar::updateExternalAddressesLabel()
{
    // 获取会话中记录的外部 IPv4 地址、IPv6 地址及对应的端口
    const QString lastExternalIPv4Address = BitTorrent::Session::instance()->lastExternalIPv4Address();
    const QString lastExternalIPv6Address = BitTorrent::Session::instance()->lastExternalIPv6Address();
    const QString lastExternalIPv4Port = BitTorrent::Session::instance()->lastExternalIPv4Port();
    const QString lastExternalIPv6Port = BitTorrent::Session::instance()->lastExternalIPv6Port();
    QString addressText = tr("External IP: N/A");  // 默认显示不可用

    const bool hasIPv4Address = !lastExternalIPv4Address.isEmpty();
    const bool hasIPv6Address = !lastExternalIPv6Address.isEmpty();

    // 根据 IPv4/IPv6 的可用情况构建显示文本（包含地址和端口）
    // 同时有 IPv4 和 IPv6 时显示两个地址及端口
    if (hasIPv4Address && hasIPv6Address)
        addressText = tr("External IPs: %1:%2, %3:%4").arg(lastExternalIPv4Address, lastExternalIPv4Port, lastExternalIPv6Address, lastExternalIPv6Port);
    else if (hasIPv4Address)  // 只有 IPv4
        addressText = tr("External IP: %1:%2").arg(lastExternalIPv4Address, lastExternalIPv4Port);
    else if (hasIPv6Address)  // 只有 IPv6
        addressText = tr("External IP: %1:%2").arg(lastExternalIPv6Address, lastExternalIPv6Port);

    m_lastExternalIPsLbl->setText(addressText);
}

// 根据用户设置显示或隐藏外部 IP 地址标签及其分隔线
void StatusBar::updateExternalAddressesVisibility()
{
    const bool isVisible = Preferences::instance()->isStatusbarExternalIPDisplayed();
    m_lastExternalIPsLbl->setVisible(isVisible);
    m_lastExternalIPsSeparator->setVisible(isVisible);
}

// 更新下载速度和上传速度显示标签
void StatusBar::updateSpeedLabels()
{
    // 获取当前会话状态
    const BitTorrent::SessionStatus& sessionStatus = BitTorrent::Session::instance()->status();

    // 处理下载速度标签
    QString dlSpeedLbl = Utils::Misc::friendlyUnit(sessionStatus.payloadDownloadRate, true);  // 当前下载速率
    const int dlSpeedLimit = BitTorrent::Session::instance()->downloadSpeedLimit();           // 下载速度限制（0 表示无限制）
    if (dlSpeedLimit > 0)  // 如果有限速，在标签中显示限制值（用方括号括起）
        dlSpeedLbl += u" [" + Utils::Misc::friendlyUnit(dlSpeedLimit, true) + u']';
    // 累计下载总量（用圆括号括起）
    dlSpeedLbl += u" (" + Utils::Misc::friendlyUnit(sessionStatus.totalPayloadDownload) + u')';
    m_dlSpeedLbl->setText(dlSpeedLbl);

    // 处理上传速度标签（逻辑同上）
    QString upSpeedLbl = Utils::Misc::friendlyUnit(sessionStatus.payloadUploadRate, true);
    const int upSpeedLimit = BitTorrent::Session::instance()->uploadSpeedLimit();
    if (upSpeedLimit > 0)
        upSpeedLbl += u" [" + Utils::Misc::friendlyUnit(upSpeedLimit, true) + u']';
    upSpeedLbl += u" (" + Utils::Misc::friendlyUnit(sessionStatus.totalPayloadUpload) + u')';
    m_upSpeedLbl->setText(upSpeedLbl);
}

// 刷新状态栏的所有信息（连接状态、DHT 节点数、外部 IP、速度）
void StatusBar::refresh()
{
    updateConnectionStatus();      // 更新连接状态图标和提示
    updateDHTNodesNumber();        // 更新 DHT 节点数量
    updateExternalAddressesLabel(); // 更新外部 IP 地址
    updateSpeedLabels();           // 更新上传/下载速度
}

// 根据是否启用备用速度限制更新对应按钮的图标、提示和按压状态
void StatusBar::updateAltSpeedsBtn(bool alternative)
{
    if (alternative)  // 启用备用速度限制
    {
        m_altSpeedsBtn->setIcon(UIThemeManager::instance()->getIcon(u"slow"_s));      // 乌龟图标（慢速）
        m_altSpeedsBtn->setToolTip(tr("Click to switch to regular speed limits"));   // 点击切换回常规速度
        m_altSpeedsBtn->setDown(true);  // 按钮表现为按下状态
    }
    else              // 未启用备用速度限制（使用常规速度）
    {
        m_altSpeedsBtn->setIcon(UIThemeManager::instance()->getIcon(u"slow_off"_s));  // 乌龟关闭图标
        m_altSpeedsBtn->setToolTip(tr("Click to switch to alternative speed limits")); // 点击切换到备用速度
        m_altSpeedsBtn->setDown(false); // 按钮未按下
    }
    refresh();  // 刷新状态栏（因为速度限制变化可能影响速度显示中的限制值）
}

// 槽函数：点击速度标签时打开速度限制设置对话框
void StatusBar::capSpeed()
{
    // 创建 SpeedLimitDialog 对话框，父窗口为状态栏的父窗口（通常是 MainWindow）
    auto* dialog = new SpeedLimitDialog{ parentWidget() };
    dialog->setAttribute(Qt::WA_DeleteOnClose);  // 对话框关闭时自动销毁
    dialog->open();  // 以非模态方式打开对话框
}

// 槽函数：用户保存选项后调用，更新状态栏中受设置影响的控件的可见性
void StatusBar::optionsSaved()
{
    updateFreeDiskSpaceVisibility();   // 根据新设置显示/隐藏可用磁盘空间标签
    updateExternalAddressesVisibility(); // 根据新设置显示/隐藏外部 IP 地址标签
}



//my
// 新增：实现外部IP标签的右键菜单及跳转
void StatusBar::showExternalIpMenu(const QPoint& pos)
{
    // Get the current IPv4 and IPv6 addresses
    const QString lastExternalIPv4Address = BitTorrent::Session::instance()->lastExternalIPv4Address();
    const QString lastExternalIPv6Address = BitTorrent::Session::instance()->lastExternalIPv6Address();
    const QString lastExternalIPv4Port = BitTorrent::Session::instance()->lastExternalIPv4Port();
    const QString lastExternalIPv6Port = BitTorrent::Session::instance()->lastExternalIPv6Port();

    // If neither IP is available, do nothing
    if (lastExternalIPv4Address.isEmpty() && lastExternalIPv6Address.isEmpty())
        return;

    QMenu menu(this);

    // Actions for each test website, separated by IP version
    QAction* actionIPv4_itdog = nullptr;
    QAction* actionIPv6_itdog = nullptr;
    QAction* actionIPv4_pingpe = nullptr;
    QAction* actionIPv6_pingpe = nullptr;
    QAction* actionIPv4_antping = nullptr;
    QAction* actionIPv6_antping = nullptr;

    // --- tcp.ping.pe / tcp6.ping.pe ---
    if (!lastExternalIPv4Address.isEmpty()) {
        actionIPv4_pingpe = menu.addAction(
            tr("Test IPv4 (ping.pe) - %1:%2").arg(lastExternalIPv4Address, lastExternalIPv4Port));
    }
    if (!lastExternalIPv6Address.isEmpty()) {
        actionIPv6_pingpe = menu.addAction(
            tr("Test IPv6 (ping.pe) - [%1]:%2").arg(lastExternalIPv6Address, lastExternalIPv6Port));
    }

    // --- antping.com ---
    if (!lastExternalIPv4Address.isEmpty()) {
        actionIPv4_antping = menu.addAction(
            tr("Test IPv4 (antping.com) - %1:%2").arg(lastExternalIPv4Address, lastExternalIPv4Port));
    }
    if (!lastExternalIPv6Address.isEmpty()) {
        actionIPv6_antping = menu.addAction(
            tr("Test IPv6 (antping.com) - [%1]:%2").arg(lastExternalIPv6Address, lastExternalIPv6Port));
    }


    // --- itdog.cn (original) ---
    if (!lastExternalIPv4Address.isEmpty()) {
        actionIPv4_itdog = menu.addAction(
            tr("Test IPv4 (itdog.cn) - %1:%2").arg(lastExternalIPv4Address, lastExternalIPv4Port));
    }
    if (!lastExternalIPv6Address.isEmpty()) {
        actionIPv6_itdog = menu.addAction(
            tr("Test IPv6 (itdog.cn) - [%1]:%2").arg(lastExternalIPv6Address, lastExternalIPv6Port));
    }


    // Show the menu and wait for selection
    QAction* selectedAction = menu.exec(m_lastExternalIPsLbl->mapToGlobal(pos));
    if (!selectedAction)
        return;

    QString targetUrl;

    // Map the selected action to the corresponding URL
    if (selectedAction == actionIPv4_itdog) {
        targetUrl = u"https://www.itdog.cn/tcping/"_s + lastExternalIPv4Address + u":"_s + lastExternalIPv4Port;
    }
    else if (selectedAction == actionIPv6_itdog) {
        targetUrl = u"https://www.itdog.cn/tcping_ipv6/["_s + lastExternalIPv6Address + u"]:"_s + lastExternalIPv6Port;
    }
    else if (selectedAction == actionIPv4_pingpe) {
        targetUrl = u"https://tcp.ping.pe/"_s + lastExternalIPv4Address + u":"_s + lastExternalIPv4Port;
    }
    else if (selectedAction == actionIPv6_pingpe) {
        targetUrl = u"https://tcp6.ping.pe/["_s + lastExternalIPv6Address + u"]:"_s + lastExternalIPv6Port;
    }
    else if (selectedAction == actionIPv4_antping) {
        targetUrl = u"https://antping.com/tcp/"_s + lastExternalIPv4Address + u":"_s + lastExternalIPv4Port;
    }
    else if (selectedAction == actionIPv6_antping) {
        targetUrl = u"https://antping.com/tcp/["_s + lastExternalIPv6Address + u"]:"_s + lastExternalIPv6Port;
    }

    // Open the URL in the default browser
    if (!targetUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(targetUrl));
    }
}
