// 本文件为 PeerListWidget 类的头文件，用于显示 BitTorrent 连接的 peers 列表

#pragma once  // 防止头文件被重复包含

#include <QHash>
#include <QSet>
#include <QTreeView>

class QHostAddress;
class QStandardItem;
class QStandardItemModel;

class PeerListSortModel;
class PropertiesWidget;

struct PeerEndpoint;

namespace BitTorrent
{
    class Torrent;
    class PeerInfo;
}

// PeerListWidget 类继承自 QTreeView，用于以表格形式展示对等点（peer）信息
class PeerListWidget final : public QTreeView
{
    Q_OBJECT  // 启用 Qt 元对象特性，支持信号槽
        Q_DISABLE_COPY_MOVE(PeerListWidget)  // 禁止复制和移动构造/赋值

public:
    // 定义表格中各列的枚举值，用于索引对应的列
    enum PeerListColumns
    {
        COUNTRY,            // 国家/地区列
        LOCATION,
        IP,                 // IP 地址列
        PORT,               // 端口列
        IP_ORGNANIZATION,

        CONNECTION,         // 连接类型列
        FLAGS,              // 标志位列
        CLIENT,             // 客户端名称列
        PEERID_CLIENT,      // Peer ID 客户端列
        PROGRESS,           // 下载进度列
        DOWN_SPEED,         // 下载速度列
        UP_SPEED,           // 上传速度列
        PEER_DOWN_SPEED,
        RTT,
        TOT_DOWN,           // 总下载量列
        TOT_UP,             // 总上传量列
        RELEVANCE,          // 相关性列
        DOWNLOADING_PIECE,  // 正在下载的分片列
        IP_HIDDEN,          // IP 隐藏状态列

        COL_COUNT           // 列的总数，用于循环或数组大小


    };

    explicit PeerListWidget(PropertiesWidget* parent);  // 构造函数，parent 为父窗口
    ~PeerListWidget() override;  // 析构函数

    // 加载指定 torrent 的 peers 列表数据并显示
    void loadPeers(const BitTorrent::Torrent* torrent);
    // 更新对端主机名解析的状态（启用/禁用）
    void updatePeerHostNameResolutionState();
    // 更新对端国家/地区解析的状态（启用/禁用）
    void updatePeerCountryResolutionState();
    // 清空当前列表中的所有条目
    void clear();

private slots:
    bool loadSettings();          // 加载设置（如列可见性、排序等）
    void saveSettings() const;    // 保存当前设置
    void displayColumnHeaderMenu(); // 显示列标题的右键菜单，用于定制列
    void showPeerListMenu();      // 显示对端列表的右键菜单
    void banSelectedPeers();      // 封禁选中的对端
    void copySelectedPeers();     // 复制选中的对端信息到剪贴板
    void copySelectedPeersLocation();
    void copySelectedPeersISP();
    void NexttraceSelectedPeer();
    void handleSortColumnChanged(int col); // 处理排序列变化
    void handleResolved(const QHostAddress& ip, const QString& hostname) const; // 处理 IP 反向解析完成的结果

private:
    // 更新指定行的对端信息显示
    void updatePeer(int row, const BitTorrent::Torrent* torrent, const BitTorrent::PeerInfo& peer, bool hideZeroValues);
    // 返回当前可见列的数量（用于布局）
    int visibleColumnsCount() const;

    void wheelEvent(QWheelEvent* event) override;  // 重写鼠标滚轮事件，实现自定义滚动行为

    QStandardItemModel* m_listModel = nullptr;   // 列表数据模型（标准项模型）
    PeerListSortModel* m_proxyModel = nullptr;   // 排序代理模型
    PropertiesWidget* m_properties = nullptr;    // 父级属性窗口指针，用于交互
    QHash<PeerEndpoint, QStandardItem*> m_peerItems;        // 对端端点 -> 列表项的映射
    QList<QStandardItem*> m_I2PPeerItems;                   // I2P 网络的对端列表项集合
    QHash<QHostAddress, QSet<QStandardItem*>> m_itemsByIP;  // IP 地址 -> 所有具有该 IP 的列表项集合（需与 m_peerItems 保持同步）
    bool m_resolveCountries = true;   // 是否解析国家/地区（通过 IP 定位）
    bool m_resolveHostNames = false;   // 是否反向解析主机名
};
