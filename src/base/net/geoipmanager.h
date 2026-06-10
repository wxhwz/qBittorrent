// 防止头文件被重复包含
#pragma once

#include <QObject>
#include <memory>
#include <vector>

class QHostAddress;
class QString;

// 对等节点（Peer）的地理信息结构体
// 包含位置（城市/省份）、组织机构（ASN/ISP）和国家代码
struct PeerGeoInfo
{
    QString location;      // 城市/省份
    QString organization;  // ASN/ISP
    QString country_iso;   // 国家代码（ISO 3166-1 alpha-2）
};

class GeoQueryService; // 前置声明：实际执行地理信息查询的核心类

namespace Net
{
    // 下载结果的结构体（可能在别处定义，这里仅作前置声明）
    struct DownloadResult;
    const QLatin1String GEODB_FOLDER("GeoDB");
    const QLatin1String NEXTTRACE_FOLDER("Nexttrace"); // 要求的 Nexttrace 目录

    // GeoIP 管理类：单例模式，负责 IP 地理信息的查询、本地数据库的加载与更新
    class GeoIPManager final : public QObject
    {
        Q_OBJECT                     // 启用 Qt 元对象系统（信号/槽等）
            Q_DISABLE_COPY_MOVE(GeoIPManager)  // 禁止拷贝和移动构造/赋值

    public:
        // 初始化单例实例（线程不安全，需在主线程调用）
        static void initInstance();
        // 释放单例实例，销毁对象
        static void freeInstance();
        // 获取单例实例指针
        static GeoIPManager* instance();

        // 根据 IP 地址查询地理信息，返回 PeerGeoInfo 结构体
        PeerGeoInfo lookup(const QHostAddress& hostAddr) const;

        // 根据国家代码（ISO 代码）获取完整国家名称的静态方法
        static QString CountryName(const QString& countryISOCode);

    private slots:
        // 槽函数：执行配置操作（如读取设置、决定是否启用 GeoIP）
        void configure();
        // 槽函数：当数据库文件下载完成时调用，处理下载结果
        void downloadFinished(const DownloadResult& result);
        void nexttraceDownloadFinished(const DownloadResult& result);

    private:
        // 构造函数私有化（单例模式）
        GeoIPManager();
        // 析构函数私有化（单例模式）
        ~GeoIPManager() override;

        // 从本地文件加载数据库到 m_geoQueryService
        void loadDatabase();
        //// 管理数据库更新（检查更新、决定是否下载新文件）
        //void manageDatabaseUpdate();
        //// 开始下载数据库文件（通常会启动 HTTP 请求）
        //void downloadDatabaseFile();
        // 辅助函数：用于控制多个数据库文件（最多 5 个）的顺序下载
        void downloadNextFile();

        // 新增：支持按队列下载特定文件的函数
        void downloadSpecificFiles(const std::vector<int>& fileIndices);

        bool m_nexttraceDownloading = false;
        void checkAndUpdateNexttrace();

        // 是否启用 GeoIP 功能（可由配置开关控制）
        bool m_enabled = false;
        // 实际执行地理查询的服务对象（采用智能指针管理）
        std::unique_ptr<GeoQueryService> m_geoQueryService = nullptr;

        // 当前正在下载的文件索引（-1 表示未开始，0~4 分别对应 5 个可能的文件）
        int m_currentDownloadIndex = -1;

        std::vector<int> m_pendingDownloads; // 新增：保存当前需要按需下载的文件索引队列

        // 单例实例的静态指针
        static GeoIPManager* m_instance;
    };
}
