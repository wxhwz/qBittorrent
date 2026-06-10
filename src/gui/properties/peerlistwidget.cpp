#include "peerlistwidget.h"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QFuture>
#include <QHeaderView>
#include <QHostAddress>
#include <QList>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QWheelEvent>
#include <QVboxLayout>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include <QProcess>

#include "base/bittorrent/peeraddress.h"
#include "base/bittorrent/peerinfo.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrent.h"
#include "base/bittorrent/torrentimpl.h"
#include "base/bittorrent/torrentinfo.h"
#include "base/global.h"
#include "base/logger.h"
#include "base/profile.h"
#include "base/net/geoipmanager.h"
#include "base/net/reverseresolution.h"
#include "base/preferences.h"
#include "base/utils/misc.h"
#include "base/utils/string.h"
#include "gui/uithememanager.h"
#include "gui/utils/keysequence.h"
#include "peerlistsortmodel.h"
#include "peersadditiondialog.h"
#include "propertieswidget.h"



struct PeerEndpoint
{
    BitTorrent::PeerAddress address;
    QString connectionType; // matches return type of `PeerInfo::connectionType()`

    friend bool operator==(const PeerEndpoint& left, const PeerEndpoint& right) = default;
};

std::size_t qHash(const PeerEndpoint& peerEndpoint, const std::size_t seed = 0)
{
    return qHashMulti(seed, peerEndpoint.address, peerEndpoint.connectionType);
}

namespace
{
    // 辅助函数：方便地将多项数据一次性设置到数据模型的特定单元格（行、列）中
    void setModelData(QStandardItemModel* model, const int row, const int column, const QString& displayData
        , const QVariant& underlyingData, const Qt::Alignment textAlignmentData = {}, const QString& toolTip = {})
    {
        const QMap<int, QVariant> data = {
            {Qt::DisplayRole, displayData},                            // 界面显示的文本
            {PeerListSortModel::UnderlyingDataRole, underlyingData},    // 用于排序/过滤的底层原始数据
            {Qt::TextAlignmentRole, QVariant {textAlignmentData}},      // 文本对齐方式
            {Qt::ToolTipRole, toolTip} };                                // 鼠标悬停时的提示文本

        model->setItemData(model->index(row, column), data);
    }
}

// 构造函数
PeerListWidget::PeerListWidget(PropertiesWidget* parent)
    : QTreeView(parent)
    , m_properties(parent)
{
    // 加载用户之前保存的列宽、显示状态等设置
    const bool columnLoaded = loadSettings();

    // 界面视觉效果配置
    setUniformRowHeights(true);                         // 设置所有行高一致（提升性能）
    setRootIsDecorated(false);                          // 不显示树状图的展开/折叠箭头
    setItemsExpandable(false);                          // 节点不可展开
    setAllColumnsShowFocus(true);                       // 选中时整行高亮
    setEditTriggers(QAbstractItemView::NoEditTriggers); // 禁用双击编辑单元格
    setSelectionMode(QAbstractItemView::ExtendedSelection); // 支持多选（按住 Ctrl/Shift 选多个）
    header()->setFirstSectionMovable(true);             // 允许移动第一列
    header()->setStretchLastSection(false);             // 最后一列不自动拉伸填满
    header()->setTextElideMode(Qt::ElideRight);         // 文本过长时末尾显示省略号(...)

    header()->setMinimumSectionSize(10);                //将列的最小宽度强行设置为 10 像素

    // 初始化列表数据模型，配置总列数
    m_listModel = new QStandardItemModel(0, PeerListColumns::COL_COUNT, this);
    // 设置各列的表头国际化文本
    m_listModel->setHeaderData(PeerListColumns::COUNTRY, Qt::Horizontal, tr("Country/Region")); // 国旗/地区列
    m_listModel->setHeaderData(PeerListColumns::IP, Qt::Horizontal, tr("IP/Address"));
    m_listModel->setHeaderData(PeerListColumns::PORT, Qt::Horizontal, tr("Port"));
    // === [新增 3] ===
    // 设置新列的表头名称（支持多语言 tr，这里直接写死或用你需要的字符串）
    m_listModel->setHeaderData(PeerListColumns::LOCATION, Qt::Horizontal, tr("Location"));
    m_listModel->setHeaderData(PeerListColumns::IP_ORGNANIZATION, Qt::Horizontal, tr("ISP/Organization"));
    //m_listModel->setHeaderData(PeerListColumns::PEER_DOWN_SPEED, Qt::Horizontal, tr("Peer Down Speed"));
    //m_listModel->setHeaderData(PeerListColumns::PEER_DOWN_SPEED, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_listModel->setHeaderData(PeerListColumns::PEER_DOWN_SPEED, Qt::Horizontal, tr("Peer DL Speed"));
    m_listModel->setHeaderData(PeerListColumns::PEER_DOWN_SPEED, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_listModel->setHeaderData(PeerListColumns::RTT, Qt::Horizontal, tr("RTT"));
    // ===============

    m_listModel->setHeaderData(PeerListColumns::FLAGS, Qt::Horizontal, tr("Flags")); // BT 标识（如 D=正在下载, U=正在上传）
    m_listModel->setHeaderData(PeerListColumns::CONNECTION, Qt::Horizontal, tr("Connection")); // 连接类型
    m_listModel->setHeaderData(PeerListColumns::CLIENT, Qt::Horizontal, tr("Client", "i.e.: Client application")); // 客户端名称
    m_listModel->setHeaderData(PeerListColumns::PEERID_CLIENT, Qt::Horizontal, tr("Peer ID", "i.e.: Client resolved from Peer ID")); // 从 Peer ID 解析出的客户端
    m_listModel->setHeaderData(PeerListColumns::PROGRESS, Qt::Horizontal, tr("Progress", "i.e: % downloaded")); // 对方的进度（%）
    m_listModel->setHeaderData(PeerListColumns::DOWN_SPEED, Qt::Horizontal, tr("Down Speed", "i.e: Download speed")); // 下载速度（我们从该 Peer 下载）
    m_listModel->setHeaderData(PeerListColumns::UP_SPEED, Qt::Horizontal, tr("Up Speed", "i.e: Upload speed")); // 上传速度（我们上传给该 Peer）
    m_listModel->setHeaderData(PeerListColumns::TOT_DOWN, Qt::Horizontal, tr("Downloaded", "i.e: total data downloaded")); // 累计下载量
    m_listModel->setHeaderData(PeerListColumns::TOT_UP, Qt::Horizontal, tr("Uploaded", "i.e: total data uploaded")); // 累计上传量
    m_listModel->setHeaderData(PeerListColumns::RELEVANCE, Qt::Horizontal, tr("Relevance", "i.e: How relevant this peer is to us. How many pieces it has that we don't.")); // 关联度（对方拥有多少我们没有的数据块）
    m_listModel->setHeaderData(PeerListColumns::DOWNLOADING_PIECE, Qt::Horizontal, tr("Files", "i.e. files that are being downloaded right now")); // 正在下载的文件名

    // 设置特定列（数值型）的表头文本靠右对齐
    m_listModel->setHeaderData(PeerListColumns::PORT, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_listModel->setHeaderData(PeerListColumns::PROGRESS, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_listModel->setHeaderData(PeerListColumns::DOWN_SPEED, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_listModel->setHeaderData(PeerListColumns::UP_SPEED, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_listModel->setHeaderData(PeerListColumns::TOT_DOWN, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_listModel->setHeaderData(PeerListColumns::TOT_UP, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_listModel->setHeaderData(PeerListColumns::RELEVANCE, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);

    // 初始化排序/过滤代理模型（不直接修改底层数据模型，从而支持高效排序）
    m_proxyModel = new PeerListSortModel(this);
    m_proxyModel->setDynamicSortFilter(true); // 启用动态排序过滤
    m_proxyModel->setSourceModel(m_listModel); // 绑定底层原始模型
    m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive); // 排序忽略大小写
    setModel(m_proxyModel); // 将代理模型设置给 TreeView

    // 隐藏无需在界面显示的辅助计算列
    hideColumn(PeerListColumns::IP_HIDDEN);
    hideColumn(PeerListColumns::COL_COUNT);

    // 如果是第一次启动，没有加载到历史界面设置，默认隐藏 "Peer ID Client" 列
    if (!columnLoaded)
    {
        hideColumn(PeerListColumns::PEERID_CLIENT);
    }

    // 根据全局设置：是否解析 Peer 所在的国家/地区
    m_resolveCountries = Preferences::instance()->resolvePeerCountries();
    if (!m_resolveCountries)
        hideColumn(PeerListColumns::COUNTRY); // 没开启就隐藏国旗列

    // 界面安全保障机制：确保至少有一列是可见的，避免界面全空导致卡死
    bool atLeastOne = false;
    for (int i = 0; i < PeerListColumns::IP_HIDDEN; ++i)
    {
        if (!isColumnHidden(i))
        {
            atLeastOne = true;
            break;
        }
    }
    if (!atLeastOne)
        setColumnHidden(PeerListColumns::IP, false); // 如果全隐藏了，强行把 IP 列显示出来

    // 修复 Qt 边界缺陷：如果一列的显示宽度恢复后小于等于 0，显式调用“自适应内容宽度”来撑开它
    for (int i = 0; i < PeerListColumns::IP_HIDDEN; ++i)
    {
        if ((columnWidth(i) <= 0) && !isColumnHidden(i))
            resizeColumnToContents(i);
    }

    // 配置右键菜单机制
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &PeerListWidget::showPeerListMenu);

    // 启用点击表头自动排序
    setSortingEnabled(true);

    // 初始化 IP 反查域名（Hostname）的状态
    updatePeerHostNameResolutionState();

    // 绑定表头相关的各种信号与槽（保存列宽变动、排序变动、移动列位置等设置）
    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header(), &QWidget::customContextMenuRequested, this, &PeerListWidget::displayColumnHeaderMenu);
    connect(header(), &QHeaderView::sectionClicked, this, &PeerListWidget::handleSortColumnChanged);
    connect(header(), &QHeaderView::sectionMoved, this, &PeerListWidget::saveSettings);
    connect(header(), &QHeaderView::sectionResized, this, &PeerListWidget::saveSettings);
    connect(header(), &QHeaderView::sortIndicatorChanged, this, &PeerListWidget::saveSettings);

    // 触发一次排序规则的调整
    handleSortColumnChanged(header()->sortIndicatorSection());

    // 绑定快捷键 Ctrl+C：复制选中 Peer 的 IP:Port 信息
    const auto* copyHotkey = new QShortcut(QKeySequence::Copy, this, nullptr, nullptr, Qt::WidgetShortcut);
    connect(copyHotkey, &QShortcut::activated, this, &PeerListWidget::copySelectedPeers);

    // 绑定快捷键 Delete：永久封禁选中的 Peer
    const auto* deleteHotkey = new QShortcut(Utils::KeySequence::deleteItem(), this, nullptr, nullptr, Qt::WidgetShortcut);
    connect(deleteHotkey, &QShortcut::activated, this, &PeerListWidget::banSelectedPeers);
}

// 析构函数：组件销毁时保存当前的列宽和显示状态
PeerListWidget::~PeerListWidget()
{
    saveSettings();
}

// 弹出右键点击表头时显示的菜单（用来勾选显示/隐藏哪些列）
void PeerListWidget::displayColumnHeaderMenu()
{
    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose); // 菜单关闭时自动销毁对象
    menu->setTitle(tr("Column visibility"));
    menu->setToolTipsVisible(true);

    // 循环遍历添加可控制可见性的列
    for (int i = 0; i < PeerListColumns::IP_HIDDEN; ++i)
    {
        // 如果全局设置里关闭了国家解析，就不在菜单里显示“国家/地区”这一项
        if ((i == PeerListColumns::COUNTRY) && !Preferences::instance()->resolvePeerCountries())
            continue;

        const auto columnName = m_listModel->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
        // 添加复选框菜单项
        QAction* action = menu->addAction(columnName, this, [this, i](const bool checked)
            {
                // 保护机制：如果当前只剩一列可见，就不允许再取消勾选了
                if (!checked && (visibleColumnsCount() <= 1))
                    return;

                setColumnHidden(i, !checked); // 设置隐藏或显示

                // 如果用户选择了显示该列，但宽度太窄（小于5像素），自动扩充一下宽度以看清内容
                if (checked && (columnWidth(i) <= 5))
                    resizeColumnToContents(i);

                saveSettings(); // 保存配置
            });
        action->setCheckable(true);
        action->setChecked(!isColumnHidden(i)); // 依据当前列是否隐藏，设置勾选状态
    }

    menu->addSeparator(); // 菜单分割线
    // 添加一个快捷选项：“自动调整所有列宽”
    QAction* resizeAction = menu->addAction(tr("Resize columns"), this, [this]()
        {
            for (int i = 0, count = header()->count(); i < count; ++i)
            {
                if (!isColumnHidden(i))
                    resizeColumnToContents(i);
            }
            saveSettings();
        });
    resizeAction->setToolTip(tr("Resize all non-hidden columns to the size of their contents"));

    menu->popup(QCursor::pos()); // 在鼠标当前位置弹出菜单
}

// 更新 IP 反查域名的设置状态
void PeerListWidget::updatePeerHostNameResolutionState()
{
    const bool resolveHostNames = Preferences::instance()->resolvePeerHostNames();
    if (resolveHostNames == m_resolveHostNames)
        return; // 状态没变，直接返回

    m_resolveHostNames = resolveHostNames;
    if (m_resolveHostNames)
    {
        // 开启反查：连接反查引擎的信号，并重新加载 Peer
        connect(Net::ReverseResolution::instance(), &Net::ReverseResolution::ipResolved
            , this, &PeerListWidget::handleResolved);
        loadPeers(m_properties->getCurrentTorrent());
    }
    else
    {
        // 关闭反查：断开信号连接
        disconnect(Net::ReverseResolution::instance(), &Net::ReverseResolution::ipResolved
            , this, &PeerListWidget::handleResolved);
    }
}

// 更新国家/地区解析的设置状态
void PeerListWidget::updatePeerCountryResolutionState()
{
    const bool resolveCountries = Preferences::instance()->resolvePeerCountries();
    if (resolveCountries == m_resolveCountries)
        return;

    m_resolveCountries = resolveCountries;
    if (m_resolveCountries)
    {
        loadPeers(m_properties->getCurrentTorrent());
        showColumn(PeerListColumns::COUNTRY); // 显示国旗列
        if (columnWidth(PeerListColumns::COUNTRY) <= 0)
            resizeColumnToContents(PeerListColumns::COUNTRY);
    }
    else
    {
        hideColumn(PeerListColumns::COUNTRY); // 隐藏国旗列
    }
}

// 弹出对列表右键点击时显示的 Peer 操作上下文菜单
void PeerListWidget::showPeerListMenu()
{
    BitTorrent::Torrent* torrent = m_properties->getCurrentTorrent();
    if (!torrent) return; // 当前没有选中种子，直接返回

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setToolTipsVisible(true);

    // 1. 菜单项：“添加 Peer...”
    QAction* addNewPeer = menu->addAction(UIThemeManager::instance()->getIcon(u"peers-add"_s), tr("Add peers...")
        , this, [this, torrent]()
        {
            // 弹出弹窗让用户手动输入 IP:Port 列表
            const QList<BitTorrent::PeerAddress> peersList = PeersAdditionDialog::askForPeers(this);
            // 尝试连接并统计成功添加的数量
            const int peerCount = std::ranges::count_if(peersList, [torrent](const BitTorrent::PeerAddress& peer)
                {
                    return torrent->connectPeer(peer);
                });

            // 弹窗提示结果
            if (peerCount < peersList.length())
                QMessageBox::information(this, tr("Adding peers"), tr("Some peers cannot be added. Check the Log for details."));
            else if (peerCount > 0)
                QMessageBox::information(this, tr("Adding peers"), tr("Peers are added to this torrent."));
        });

    // 2. 菜单项：“复制 IP:端口”
    QAction* copyPeers = menu->addAction(UIThemeManager::instance()->getIcon(u"edit-copy"_s), tr("Copy IP:port")
        , this, &PeerListWidget::copySelectedPeers);
    menu->addSeparator();

    // 菜单项：“复制 location”
    QAction* copyPeersLocation = menu->addAction(UIThemeManager::instance()->getIcon(u"edit-copy"_s), tr("Copy Location")
        , this, &PeerListWidget::copySelectedPeersLocation);
    menu->addSeparator();

    // 菜单项：“复制 location”
    QAction* copyPeersISP = menu->addAction(UIThemeManager::instance()->getIcon(u"edit-copy"_s), tr("Copy ISP")
        , this, &PeerListWidget::copySelectedPeersISP);
    menu->addSeparator();

    // 菜单项：“运行nexttrace”
    QAction* nexttracePeer = menu->addAction(QIcon(), tr("Trace IP")
        , this, &PeerListWidget::NexttraceSelectedPeer);
    menu->addSeparator();

    // 3. 菜单项：“永久封禁 Peer”
    QAction* banPeers = menu->addAction(UIThemeManager::instance()->getIcon(u"peers-remove"_s), tr("Ban peer permanently")
        , this, &PeerListWidget::banSelectedPeers);

    // 内部 Lambda 表达式：用于方便地禁用某个菜单项并附带解释原因的悬停提示
    const auto disableAction = [](QAction* action, const QString& tooltip)
        {
            action->setEnabled(false);
            action->setToolTip(tooltip);
        };

    // 依据种子的状态，判断是否允许手动加 Peer
    if (torrent->isPrivate())
        disableAction(addNewPeer, tr("Cannot add peers to a private torrent")); // 私有种子（PT）禁止手动加 Peer
    else if (torrent->isChecking())
        disableAction(addNewPeer, tr("Cannot add peers when the torrent is checking")); // 正在校验文件时禁止
    else if (torrent->isQueued())
        disableAction(addNewPeer, tr("Cannot add peers when the torrent is queued")); // 正在排队时禁止

    // 如果用户右键点击时没有选中列表里的任何 Peer，禁用“复制”和“封禁”选项
    if (selectionModel()->selectedRows().isEmpty())
    {
        const QString tooltip = tr("No peer was selected");
        disableAction(copyPeers, tooltip);
        disableAction(banPeers, tooltip);
        disableAction(copyPeersLocation, tooltip);
        disableAction(copyPeersISP, tooltip);
        disableAction(nexttracePeer, tooltip);
    }

    menu->popup(QCursor::pos());
}

// 槽函数：永久封禁被选中的 Peer
void PeerListWidget::banSelectedPeers()
{
    // 先把选中的行索引存下来，因为后续操作断开连接可能导致行索引在动态更新时发生漂移
    const QModelIndexList selectedIndexes = selectionModel()->selectedRows();

    QList<QString> selectedIPs;
    selectedIPs.reserve(selectedIndexes.size());

    // 提取选中行的 IP 地址
    for (const QModelIndex& index : selectedIndexes)
    {
        const int row = m_proxyModel->mapToSource(index).row(); // 从代理模型索引映射回原始模型行号
        const QString ip = m_listModel->item(row, PeerListColumns::IP_HIDDEN)->text();
        selectedIPs += ip;
    }

    // 弹出确认弹窗，防止误操作
    const QMessageBox::StandardButton btn = QMessageBox::question(this, tr("Ban peer permanently")
        , tr("Are you sure you want to permanently ban the selected peers?"));
    if (btn != QMessageBox::Yes) return;

    // 遍历 IP 列表执行封禁并写入软件日志
    for (const QString& ip : selectedIPs)
    {
        BitTorrent::Session::instance()->banIP(ip); // 把 IP 丢进 BT 会话封禁黑名单
        LogMsg(tr("Peer \"%1\" is manually banned").arg(ip));
    }
    // 重新加载刷新列表
    loadPeers(m_properties->getCurrentTorrent());
}

// 槽函数：复制选中的 Peer 文本信息到系统剪贴板
void PeerListWidget::copySelectedPeers()
{
    const QModelIndexList selectedIndexes = selectionModel()->selectedRows();
    QStringList selectedPeers;

    for (const QModelIndex& index : selectedIndexes)
    {
        const int row = m_proxyModel->mapToSource(index).row();
        const QString ip = m_listModel->item(row, PeerListColumns::IP_HIDDEN)->text();
        const QString port = m_listModel->item(row, PeerListColumns::PORT)->text();

        // 格式化：IPv6 需要加上中括号包裹（例如 [2001:db8::1]:8080），IPv4 则直接拼接
        if (!ip.contains(u'.'))  // 如果 IP 里不包含小圆点，断定为 IPv6
            selectedPeers << (u'[' + ip + u"]:" + port);
        else  // IPv4
            selectedPeers << (ip + u':' + port);
    }

    // 用换行符连接成一段多行文本，送入剪贴板
    QApplication::clipboard()->setText(selectedPeers.join(u'\n'));
}


void PeerListWidget::copySelectedPeersLocation()
{
    const QModelIndexList selectedIndexes = selectionModel()->selectedRows();
    QStringList selectedPeersLocation;

    for (const QModelIndex& index : selectedIndexes)
    {
        const int row = m_proxyModel->mapToSource(index).row();
        // 使用 data() 安全获取文本，即使该单元格从未被赋值，也只会返回空 QVariant
        const QString location = m_listModel->data(m_listModel->index(row, PeerListColumns::LOCATION)).toString();

        // 可选：过滤掉空白数据，避免剪贴板出现过多无意义的空行
        if (!location.isEmpty())
            selectedPeersLocation << location;
    }

    if (!selectedPeersLocation.isEmpty())
        QApplication::clipboard()->setText(selectedPeersLocation.join(u'\n'));
}

void PeerListWidget::copySelectedPeersISP()
{
    const QModelIndexList selectedIndexes = selectionModel()->selectedRows();
    QStringList selectedPeersISP;

    for (const QModelIndex& index : selectedIndexes)
    {
        const int row = m_proxyModel->mapToSource(index).row();
        const QString isp = m_listModel->data(m_listModel->index(row, PeerListColumns::IP_ORGNANIZATION)).toString();

        if (!isp.isEmpty())
            selectedPeersISP << isp;
    }

    if (!selectedPeersISP.isEmpty())
        QApplication::clipboard()->setText(selectedPeersISP.join(u'\n'));
}


void PeerListWidget::NexttraceSelectedPeer() {
    const QModelIndexList selectedIndexes = selectionModel()->selectedRows();
    if (selectedIndexes.isEmpty()) return;

    const QModelIndex sourceIndex = m_proxyModel->mapToSource(selectedIndexes.first());
    if (!sourceIndex.isValid()) return;

    const int row = sourceIndex.row();
    auto* item = m_listModel->item(row, PeerListColumns::IP_HIDDEN);
    if (!item) return;

    QString ip = item->text();
    if (ip.isEmpty()) return;

    // 1. 动态构建文件名 (需与 GeoIPManager 中的逻辑一致)
    QString osStr, archStr, extStr;
#if defined(Q_OS_WIN)
    osStr = QStringLiteral("windows");
    extStr = QStringLiteral(".exe");
#elif defined(Q_OS_MACOS)
    osStr = QStringLiteral("darwin");
#else // Linux
    osStr = QStringLiteral("linux");
#endif

    QString currentArch = QSysInfo::currentCpuArchitecture();
    if (currentArch.contains(QStringLiteral("64"))) archStr = QStringLiteral("amd64");
    else if (currentArch.contains(QStringLiteral("arm"))) archStr = QStringLiteral("arm64");
    else archStr = QStringLiteral("386");

    QString filename = QStringLiteral("nexttrace-tiny_%1_%2%3").arg(osStr, archStr, extStr);

    // 2. 确定完整路径
    const Path nextTracePath = specialFolderLocation(SpecialFolder::Data) / Path(Net::NEXTTRACE_FOLDER) / Path(filename);
    QString absPath = nextTracePath.toString();
    //LogMsg(u"%1"_s.arg(absPath));

    // 3. 跨平台启动逻辑
    QString program;
    QStringList args;
#if defined(Q_OS_WIN)
    // Windows: 使用 cmd /c start 新窗口
    program = QStringLiteral("cmd.exe");
    args << QStringLiteral("/c") << QStringLiteral("start")
        << QStringLiteral("cmd.exe") << QStringLiteral("/k")
        << absPath << ip;

#elif defined(Q_OS_MACOS)
    // macOS: 使用 open -a Terminal 启动新窗口
    program = QStringLiteral("open");
    args << QStringLiteral("-a") << QStringLiteral("Terminal") << absPath << QStringLiteral("--args") << ip;

#else
    // Linux: 尝试常见的终端模拟器
    program = QStringLiteral("sh");
    // 使用 xterm 或 gnome-terminal 等，配合 -e 执行命令
    QString cmd = QStringLiteral("%1 %2; read -p 'Press Enter to exit...'").arg(absPath, ip);
    args << QStringLiteral("-c") << QStringLiteral("xterm -e \"%1\"").arg(cmd);
#endif

    // 4. 以 Detached 方式启动，确保进程独立
    if (!QProcess::startDetached(program, args)) {
        LogMsg(tr("Failed to start Nexttrace process: %1").arg(absPath), Log::WARNING);
    }
}
// 清空界面上的所有 Peer 缓存和行数据
void PeerListWidget::clear()
{
    m_peerItems.clear();
    m_I2PPeerItems.clear();
    m_itemsByIP.clear();

    const int nbrows = m_listModel->rowCount();
    if (nbrows > 0)
        m_listModel->removeRows(0, nbrows);
}

// 读取历史存储的界面排列状态
bool PeerListWidget::loadSettings()
{
    return header()->restoreState(Preferences::instance()->getPeerListState());
}

// 存储当前的界面排列状态
void PeerListWidget::saveSettings() const
{
    Preferences::instance()->setPeerListState(header()->saveState());
}

// 核心函数：根据当前种子加载/更新 Peer 数据（异步处理）
void PeerListWidget::loadPeers(const BitTorrent::Torrent* torrent)
{
    if (!torrent)
        return;

    using TorrentPtr = QPointer<const BitTorrent::Torrent>;
    // 异步拉取当前的 Peer 信息，并注册回调处理
    torrent->fetchPeerInfo().then(this, [this, torrent = TorrentPtr(torrent)](const QList<BitTorrent::PeerInfo>& peers)
        {
            // 检查防御：如果用户在异步加载期间切换到了别的种子，或者当前种子失效了，直接放弃本次更新
            const BitTorrent::Torrent* currentTorrent = m_properties->getCurrentTorrent();
            if (!currentTorrent || (currentTorrent != torrent))
            {
                return;
            }

            //QString infoHash = currentTorrent->infoHash().toString();
            ////qlonglong pieceLength = currentTorrent->pieceLength();

            // 移除旧的 I2P (隐网匿名网络) Peer，因为它们接下来会被完整重新载入
            for (const QStandardItem* item : asConst(m_I2PPeerItems))
                m_listModel->removeRow(item->row());
            m_I2PPeerItems.clear();

            // 创建一个用于比对残留老数据的集合
            QSet<PeerEndpoint> existingPeers;
            existingPeers.reserve(m_peerItems.size());
            for (auto i = m_peerItems.cbegin(); i != m_peerItems.cend(); ++i)
                existingPeers.insert(i.key());

            const Preferences* pref = Preferences::instance();
            // 判断设置：是否需要在速度/下载量为 0 时隐藏单元格内的数值显示
            const bool hideZeroValues = (pref->getHideZeroValues() && (pref->getHideZeroComboValues() == 0));

            // 遍历所有获取到的最新 Peer，将其添加到 Model 或更新已有行
            for (const BitTorrent::PeerInfo& peer : peers)
            {
                const PeerEndpoint peerEndpoint{ peer.address(), peer.connectionType() };

                auto itemIter = m_peerItems.find(peerEndpoint);
                const bool isNewPeer = (itemIter == m_peerItems.end()); // 没找到说明这是一个刚连进来的新 Peer
                const int row = isNewPeer ? m_listModel->rowCount() : (*itemIter)->row();

                if (isNewPeer)
                {
                    // 新 Peer：在底层模型插入一行新空白行
                    m_listModel->insertRow(row);

                    const bool useI2PSocket = peer.useI2PSocket();

                    // 写入 IP 列数据（区分 I2P 匿名地址和普通 IP）
                    const QString peerIPString = useI2PSocket ? peer.I2PAddress() : peerEndpoint.address.ip.toString();
                    setModelData(m_listModel, row, PeerListColumns::IP, peerIPString, peerIPString, {}, peerIPString);

                    // 写入隐藏 IP 列数据（用于内部逻辑处理）
                    const QString peerIPHiddenString = useI2PSocket ? QString() : peerEndpoint.address.ip.toString();
                    setModelData(m_listModel, row, PeerListColumns::IP_HIDDEN, peerIPHiddenString, peerIPHiddenString);

                    // 写入端口号列数据
                    const QString peerPortString = useI2PSocket ? tr("N/A") : QString::number(peer.address().port);
                    setModelData(m_listModel, row, PeerListColumns::PORT, peerPortString, peer.address().port, (Qt::AlignRight | Qt::AlignVCenter));

                    // 把该行的指针记录到检索用的数据结构中
                    if (useI2PSocket)
                    {
                        m_I2PPeerItems.append(m_listModel->item(row, PeerListColumns::IP));
                    }
                    else
                    {
                        itemIter = m_peerItems.insert(peerEndpoint, m_listModel->item(row, PeerListColumns::IP));
                        m_itemsByIP[peerEndpoint.address.ip].insert(itemIter.value());
                    }
                }
                else
                {
                    // 已存在的 Peer：从残留比对集合中剔除它，代表它依然活跃
                    existingPeers.remove(peerEndpoint);
                }

                // 更新这行数据具体的变化指标（如速度、进度等）
                updatePeer(row, torrent, peer, hideZeroValues);
            }

            // 善后：对于那些没有在最新数据里出现的 Peer，说明他们已经断开连接了，从列表中移除它们
            for (const PeerEndpoint& peerEndpoint : asConst(existingPeers))
            {
                QStandardItem* item = m_peerItems.take(peerEndpoint);

                const auto items = m_itemsByIP.find(peerEndpoint.address.ip);
                Q_ASSERT(items != m_itemsByIP.end());
                if (items == m_itemsByIP.end()) [[unlikely]]
                    continue;

                items->remove(item);
                if (items->isEmpty())
                    m_itemsByIP.erase(items);

                m_listModel->removeRow(item->row()); // 从树视图里删掉这一行
            }
        });
}

// 更新指定行的各项具体 Peer 指标（速度、流量、软件客户端等具体数据）
void PeerListWidget::updatePeer(const int row, const BitTorrent::Torrent* torrent, const BitTorrent::PeerInfo& peer, const bool hideZeroValues)
{
    const Qt::Alignment intDataTextAlignment = Qt::AlignRight | Qt::AlignVCenter; // 数值型右对齐

    if (!peer.useI2PSocket()) // 排除 I2P 匿名网络
    {
        // 使用 object() 获取指针
        const PeerGeoInfo* ipExtraInfo = peer.peerGeoInfo();

        // 安全校验：确保指针有效且内部包含足够的数据 (防止越界崩溃)
        if (ipExtraInfo) {
            setModelData(m_listModel, row, PeerListColumns::LOCATION, ipExtraInfo->location, ipExtraInfo->location);
            setModelData(m_listModel, row, PeerListColumns::IP_ORGNANIZATION, ipExtraInfo->organization, ipExtraInfo->organization);
        }

    }

    // 1. 客户端名称（进行 HTML 转义防止注入攻击引发崩溃）
    const QString client = peer.client().toHtmlEscaped();
    setModelData(m_listModel, row, PeerListColumns::CLIENT, client, client, {}, client);

    // 2. 从 Peer ID 算出来的具体客户端
    const QString peerIdClient = peer.peerIdClient().toHtmlEscaped();
    setModelData(m_listModel, row, PeerListColumns::PEERID_CLIENT, peerIdClient, peerIdClient);

    // 3. 下载速度（应用“为零隐藏”策略，并转换为人类友好单位，如 1.2 MB/s）
    const QString downSpeed = (hideZeroValues && (peer.payloadDownSpeed() <= 0))
        ? QString() : Utils::Misc::friendlyUnit(peer.payloadDownSpeed(), true);
    setModelData(m_listModel, row, PeerListColumns::DOWN_SPEED, downSpeed, peer.payloadDownSpeed(), intDataTextAlignment);

    // 4. 上传速度
    const QString upSpeed = (hideZeroValues && (peer.payloadUpSpeed() <= 0))
        ? QString() : Utils::Misc::friendlyUnit(peer.payloadUpSpeed(), true);
    setModelData(m_listModel, row, PeerListColumns::UP_SPEED, upSpeed, peer.payloadUpSpeed(), intDataTextAlignment);

    // 5. 总下载量
    const QString totalDown = (hideZeroValues && (peer.totalDownload() <= 0))
        ? QString() : Utils::Misc::friendlyUnit(peer.totalDownload());
    setModelData(m_listModel, row, PeerListColumns::TOT_DOWN, totalDown, peer.totalDownload(), intDataTextAlignment);

    // 6. 总上传量
    const QString totalUp = (hideZeroValues && (peer.totalUpload() <= 0))
        ? QString() : Utils::Misc::friendlyUnit(peer.totalUpload());
    setModelData(m_listModel, row, PeerListColumns::TOT_UP, totalUp, peer.totalUpload(), intDataTextAlignment);

    // 7. 写入连接类型和 flags 标签（附带描述 ToolTip）
    setModelData(m_listModel, row, PeerListColumns::CONNECTION, peer.connectionType(), peer.connectionType());
    setModelData(m_listModel, row, PeerListColumns::FLAGS, peer.flags(), peer.flags(), {}, peer.flagsDescription());

    // 8. 进度百分比和关联度百分比
    setModelData(m_listModel, row, PeerListColumns::PROGRESS, (Utils::String::fromDouble(peer.progress() * 100, 1) + u'%')
        , peer.progress(), intDataTextAlignment);
    setModelData(m_listModel, row, PeerListColumns::RELEVANCE, (Utils::String::fromDouble(peer.relevance() * 100, 1) + u'%')
        , peer.relevance(), intDataTextAlignment);

    // 9. 计算该 Peer 当前正在请求/下载的是哪些具体文件，用分号和换行拼接展现出来
    const PathList filePaths = torrent->info().filesForPiece(peer.downloadingPieceIndex());
    QStringList downloadingFiles;
    downloadingFiles.reserve(filePaths.size());
    for (const Path& filePath : filePaths)
        downloadingFiles.append(filePath.toString());

    const QString downloadingFilesDisplayValue = downloadingFiles.join(u';');
    setModelData(m_listModel, row, PeerListColumns::DOWNLOADING_PIECE, downloadingFilesDisplayValue
        , downloadingFilesDisplayValue, {}, downloadingFiles.join(u'\n'));

    // 10. 如果启用了 IP 反查 Hostname，调用反查引擎数据并替换原本显示的 IP 字符串
    if (!peer.useI2PSocket() && m_resolveHostNames)
    {
        const QHostAddress ipAddr = peer.address().ip;
        const QString hostName = Net::ReverseResolution::instance()->resolve(ipAddr);
        if (!hostName.isEmpty())
            setModelData(m_listModel, row, PeerListColumns::IP, hostName, hostName, {}, ipAddr.toString());
    }

    // 11. 如果启用了国家解析，加载对应的 GeoIP 国旗图标并显示
    if (m_resolveCountries)
    {
        //const QIcon icon = UIThemeManager::instance()->getFlagIcon(peer.country());
        //if (!icon.isNull())
        //{
        //    m_listModel->setData(m_listModel->index(row, PeerListColumns::COUNTRY), icon, Qt::DecorationRole);
        //    const QString countryName = Net::GeoIPManager::CountryName(peer.country());
        //    m_listModel->setData(m_listModel->index(row, PeerListColumns::COUNTRY), countryName, Qt::ToolTipRole);

        //}
        // 获取国旗图标和国家名称
        const QIcon icon = UIThemeManager::instance()->getFlagIcon(peer.peerGeoInfo()->country_iso);
        const QString countryName = Net::GeoIPManager::CountryName(peer.peerGeoInfo()->country_iso);

        if (!icon.isNull())
        {
            // 设置单元格图标（渲染在左侧）
            m_listModel->setData(m_listModel->index(row, PeerListColumns::COUNTRY), icon, Qt::DecorationRole);
        }

        // 【关键修改】设置单元格文本（渲染在图标右侧）
        m_listModel->setData(m_listModel->index(row, PeerListColumns::COUNTRY), countryName, Qt::DisplayRole);
        // 保留 ToolTip（可选）：当列宽被用户拉得太窄时，鼠标悬停依然可以看全名称
        m_listModel->setData(m_listModel->index(row, PeerListColumns::COUNTRY), countryName, Qt::ToolTipRole);

    }
    //my
    const QString peerRemoteDown = (hideZeroValues && (peer.peerRemoteDownSpeed() <= 0))
        ? QString() : Utils::Misc::friendlyUnit(peer.peerRemoteDownSpeed(), true);
    setModelData(m_listModel, row, PeerListColumns::PEER_DOWN_SPEED, peerRemoteDown, peer.peerRemoteDownSpeed(), intDataTextAlignment);

    //my
    const QString rtt = QString::asprintf("%d ms", peer.rtt());
    setModelData(m_listModel, row, PeerListColumns::RTT, rtt, rtt, intDataTextAlignment);
}

// 统计当前视图里非隐藏的、可见的列数
int PeerListWidget::visibleColumnsCount() const
{
    int count = 0;
    for (int i = 0, iMax = header()->count(); i < iMax; ++i)
    {
        if (!isColumnHidden(i))
            ++count;
    }

    return count;
}

// 槽函数：异步 IP 域名解析完成后的回调，更新表格上的 IP 展示行为域名字符串
void PeerListWidget::handleResolved(const QHostAddress& ip, const QString& hostname) const
{
    if (hostname.isEmpty())
        return;

    const QSet<QStandardItem*> items = m_itemsByIP.value(ip);
    for (QStandardItem* item : items)
        item->setData(hostname, Qt::DisplayRole);
}

// 槽函数：当用户变更了排序列时触发，动态调整底层的排序角色（Role）
void PeerListWidget::handleSortColumnChanged(const int col)
{
    if (col == PeerListColumns::COUNTRY)
        // 国家列依照 ToolTip 文本（国家英文名）排序，而不是图像本身
        m_proxyModel->setSortRole(Qt::ToolTipRole);
    else
        // 其余列统一使用之前在 setModelData 存入的 UnderlyingDataRole（原始数值数据，如 B/s 字节数），避免按格式化后的字符串进行错误排序
        m_proxyModel->setSortRole(PeerListSortModel::UnderlyingDataRole);
}

// 鼠标滚轮事件重写：实现快捷操作（按住 Shift + 鼠标滚轮 = 水平滚动列表）
void PeerListWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ShiftModifier)
    {
        event->accept(); // 拦截并接收该事件
        // 转置（Transpose）滚轮的方向，将垂直偏量变为水平偏量，再交由底层 TreeView 处理
        QWheelEvent scrollHEvent{ event->position(), event->globalPosition()
            , event->pixelDelta(), event->angleDelta().transposed(), event->buttons()
            , event->modifiers(), event->phase(), event->inverted(), event->source() };
        QTreeView::wheelEvent(&scrollHEvent);
        return;
    }

    QTreeView::wheelEvent(event);  // 没按 Shift 时保持默认的原生垂直滚动
}
