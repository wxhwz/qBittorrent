#pragma once  // 确保头文件只被编译一次

#include <libtorrent/peer_info.hpp>  // libtorrent 库的对等信息结构
#include <QCoreApplication>          // Qt 核心应用，用于支持翻译功能

#include "gui/my_calPeerDownloadRate.h"

class QBitArray;  // Qt 位数组类的前置声明
struct PeerGeoInfo;

namespace BitTorrent  // BitTorrent 命名空间
{
    struct PeerAddress;  // 对等体地址结构的前置声明

    // 对等体信息类，封装 libtorrent 的原生对等信息并提供额外功能
    class PeerInfo
    {
        Q_DECLARE_TR_FUNCTIONS(PeerInfo)  // 声明 Qt 翻译函数，使类支持国际化字符串

    public:
        PeerInfo() = default;  // 默认构造函数
        // 构造函数：从原生对等信息和全局所有分片位图构建
        PeerInfo(const lt::peer_info& nativeInfo, const QBitArray& allPieces);

        ~PeerInfo();

        // ----- 来源检测 -----
        bool fromDHT() const;   // 是否通过 DHT（分布式哈希表）发现
        bool fromPeX() const;   // 是否通过 PeX（对等体交换）发现
        bool fromLSD() const;   // 是否通过 LSD（本地服务发现）发现

        // ----- 本地感兴趣/阻塞状态 -----
        bool isInteresting() const;       // 本地是否对该对等体的数据感兴趣
        bool isChocked() const;           // 本地是否阻塞了该对等体（不向其发送数据）
        bool isRemoteInterested() const;  // 远端是否对本地的数据感兴趣
        bool isRemoteChocked() const;     // 远端是否阻塞了本地（不向本地发送数据）
        bool isSupportsExtensions() const;// 是否支持 BitTorrent 协议扩展
        bool isLocalConnection() const;   // 是否是本地网络连接（如本地子网）

        // ----- 连接与状态 -----
        bool isHandshake() const;     // 是否处于握手阶段
        bool isConnecting() const;    // 是否正在连接中
        bool isOnParole() const;      // 是否处于“假释”状态（因错误被降级）
        bool isSeed() const;          // 是否已经是做种者（拥有完整文件）

        // ----- 高级算法相关 -----
        bool optimisticUnchoke() const;  // 是否正被乐观地解除阻塞
        bool isSnubbed() const;          // 是否被“冷落”（通信不畅）
        bool isUploadOnly() const;       // 是否仅上传模式（不下载）
        bool isEndgameMode() const;      // 是否处于终局模式（快速获取最后分片）
        bool isHolepunched() const;      // 是否通过 UDP 打洞建立连接

        // ----- 套接字类型 -----
        bool useI2PSocket() const;   // 是否使用 I2P 网络套接字
        bool useUTPSocket() const;   // 是否使用 uTP 传输协议套接字
        bool useSSLSocket() const;   // 是否使用 SSL/TLS 加密套接字

        // ----- 加密方式 -----
        bool isRC4Encrypted() const;      // 是否使用 RC4 加密
        bool isPlaintextEncrypted() const;// 是否使用明文加密（如无加密）

        // ----- 信息获取接口 -----
        PeerAddress address() const;      // 获取对等体的网络地址
        QString I2PAddress() const;       // 获取 I2P 地址（若使用 I2P）
        QString client() const;           // 获取客户端标识字符串（如 "qBittorrent"）
        QString peerIdClient() const;     // 从 Peer ID 解析出的客户端名称
        qreal progress() const;           // 下载进度 [0.0, 1.0]
        int payloadUpSpeed() const;       // 上行有效数据速率（字节/秒）
        int payloadDownSpeed() const;     // 下行有效数据速率（字节/秒）
        qlonglong totalUpload() const;    // 累计上传有效数据量（字节）
        qlonglong totalDownload() const;  // 累计下载有效数据量（字节）
        QBitArray pieces() const;         // 该对等体拥有的分片位图
        QString connectionType() const;   // 连接类型描述字符串（如 "uTP", "TCP"）
        qreal relevance() const;          // 相关性评分（该对等体对下载的贡献权重）
        QString flags() const;            // 标志的简短字符串表示
        QString flagsDescription() const; // 标志的详细文本描述
        PeerGeoInfo* peerGeoInfo() const;          // 根据 IP 地址解析的
        int downloadingPieceIndex() const;// 当前正在下载的分片索引（-1 表示无）

        int rtt() const;
        int peerRemoteDownSpeed() const;

        //void setPieceLength(qlonglong pieceLength);
        //qlonglong getPieceLength();

    private:
        // ----- 私有辅助方法 -----
        qreal calcRelevance(const QBitArray& allPieces) const;  // 计算相关性评分
        void determineFlags();  // 根据原生信息生成标志字符串和描述

        // ----- 私有成员变量 -----
        lt::peer_info m_nativeInfo = {};  // libtorrent 提供的原生对等信息
        qreal m_relevance = 0;            // 计算得到的相关性值
        QString m_flags;                  // 标志字符串（如 "P L U"）
        QString m_flagsDescription;       // 标志的详细中文/英文描述

        // mutable 允许在 const 成员函数中惰性初始化
        //mutable QString m_country;        // 国家代码（缓存）
        mutable std::shared_ptr<PeerGeoInfo> m_geoInfo;
        mutable QString m_I2PAddress;     // I2P 地址（缓存）

        //qlonglong m_pieceLength = 0;

    };
}
