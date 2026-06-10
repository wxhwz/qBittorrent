#include "peerinfo.h"

#include <QBitArray>

#include "base/bittorrent/ltqbitarray.h"
#include "base/net/geoipmanager.h"
#include "base/unicodestrings.h"
#include "base/utils/bytearray.h"
#include "peeraddress.h"

using namespace BitTorrent;

// 构造函数：使用 libtorrent 原生的 peer_info 结构体和本地拥有的所有分片(Pieces)状态来初始化
PeerInfo::PeerInfo(const lt::peer_info& nativeInfo, const QBitArray& allPieces)
    : m_nativeInfo(nativeInfo) // 保存 libtorrent 原生节点信息
    , m_relevance(calcRelevance(allPieces)) // 计算该节点对本地的“关联度/实用价值”
{
    determineFlags(); // 解析并生成该节点的各种状态缩写标志（如 D, U, P 等）
}

// 检查该节点是否是通过 DHT（分布式哈希表）网络发现的
bool PeerInfo::fromDHT() const
{
    return static_cast<bool>(m_nativeInfo.source & lt::peer_info::dht);
}

// 检查该节点是否是通过 PeX（节点交换技术）发现的
bool PeerInfo::fromPeX() const
{
    return static_cast<bool>(m_nativeInfo.source & lt::peer_info::pex);
}

// 检查该节点是否是通过 LSD（本地节点发现，用于局域网）发现的
bool PeerInfo::fromLSD() const
{
    return static_cast<bool>(m_nativeInfo.source & lt::peer_info::lsd);
}

// 获取节点所在的国家/地区（通过 IP 地址进行 GeoIP 查询）
PeerGeoInfo* PeerInfo::peerGeoInfo() const
{
    if (!m_geoInfo)
        m_geoInfo = std::make_shared<PeerGeoInfo>(
            Net::GeoIPManager::instance()->lookup(address().ip)
        );
    return m_geoInfo.get();
}

// 检查本地客户端是否对该节点感兴趣（即对方有我们想要的分片）
bool PeerInfo::isInteresting() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::interesting);
}

// 检查本地客户端是否阻塞了该节点（Choked：不给对方传数据）
bool PeerInfo::isChocked() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::choked);
}

// 检查该节点是否对本地客户端感兴趣（即我们有对方想要的分片）
bool PeerInfo::isRemoteInterested() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::remote_interested);
}

// 检查该节点是否阻塞了本地客户端（Remote Choked：对方不给我们传数据）
bool PeerInfo::isRemoteChocked() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::remote_choked);
}

// 检查该节点是否支持 BT 扩展协议（Extension Protocol）
bool PeerInfo::isSupportsExtensions() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::supports_extensions);
}

// 检查是否是本地连接（通常指由本地客户端发起的出站连接）
bool PeerInfo::isLocalConnection() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::local_connection);
}

// 检查连接是否正处于握手阶段
bool PeerInfo::isHandshake() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::handshake);
}

// 检查是否正在尝试连接该节点
bool PeerInfo::isConnecting() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::connecting);
}

// 检查该节点是否处于“假释/观察”状态（如果节点曾发过坏数据，会被标记并重新校验）
bool PeerInfo::isOnParole() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::on_parole);
}

// 检查该节点是否是种子（已经拥有 100% 的数据）
bool PeerInfo::isSeed() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::seed);
}

// 检查该节点是否属于“乐观解除阻塞”的目标（BT 算法会定期随机对冷门节点解锁，以探流速）
bool PeerInfo::optimisticUnchoke() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::optimistic_unchoke);
}

// 检查该节点是否被标记为“冷落”（Snubbed：很久没给本地传数据，本地会降低其优先级）
bool PeerInfo::isSnubbed() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::snubbed);
}

// 检查该节点是否处于“只传不收”状态（比如对方是种子，或者设置了仅上传）
bool PeerInfo::isUploadOnly() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::upload_only);
}

// 检查该节点是否正处于“末端逐块阶段”（Endgame Mode：下载最后的零碎分片时，会同时向多个节点索要同个分片以防被卡住）
bool PeerInfo::isEndgameMode() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::endgame_mode);
}

// 检查是否使用了 NAT 打洞连接（Hole Punching，如通过 UT_PEX / STUN 穿透内网）
bool PeerInfo::isHolepunched() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::holepunched);
}

// 检查是否使用了 I2P 匿名网络套接字连接
bool PeerInfo::useI2PSocket() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::i2p_socket);
}

// 检查是否使用了 uTP 协议套接字（基于 UDP 的慢速拥塞控制 BT 传输协议）
bool PeerInfo::useUTPSocket() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::utp_socket);
}

// 检查是否使用了基于 SSL/TLS 的加密套接字连接
bool PeerInfo::useSSLSocket() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::ssl_socket);
}

// 检查是否使用了 RC4 流加密（BT 协议的一种传统混淆加密方式）
bool PeerInfo::isRC4Encrypted() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::rc4_encrypted);
}

// 检查是否使用了明文加密（仅对握手进行混淆，数据主体采用明文传输）
bool PeerInfo::isPlaintextEncrypted() const
{
    return static_cast<bool>(m_nativeInfo.flags & lt::peer_info::plaintext_encrypted);
}

// 获取节点的 IP 和端口网络地址
PeerAddress PeerInfo::address() const
{
    if (useI2PSocket())
        return {}; // 如果是 I2P 匿名连接，其不适用普通 IP 地址，返回空

    // 快捷路径：适用于 boost.asio 内部结构能直接映射到标准 `sockaddr` 的平台（性能更高）
    return { QHostAddress(m_nativeInfo.ip.data()), m_nativeInfo.ip.port() };
    // 慢速路径：其他平台（通过转为标准字符串中介来转换 IP 地址）
    //return {QHostAddress(QString::fromStdString(m_nativeInfo.ip.address().to_string()))
    //      , m_nativeInfo.ip.port()};
}

// 获取 I2P 匿名网络地址
QString PeerInfo::I2PAddress() const
{
    if (!useI2PSocket())
        return {}; // 没使用 I2P 连接则返回空

#if defined(QBT_USES_LIBTORRENT2) && TORRENT_USE_I2P
    if (m_I2PAddress.isEmpty())
    {
        // 提取 libtorrent 内部的 I2P 目标哈希值，并将其转换为 Base32 编码的 .b32.i2p 域名格式
        const lt::sha256_hash destHash = m_nativeInfo.i2p_destination();
        const QByteArray base32Dest = Utils::ByteArray::toBase32({ destHash.data(), destHash.size() }).replace('=', "").toLower();
        m_I2PAddress = QString::fromLatin1(base32Dest) + u".b32.i2p";
    }
#endif

    return m_I2PAddress;
}

// 获取节点客户端的名称软件（例如 "uTorrent 3.5.5" 或 "qBittorrent 4.6.0"）
QString PeerInfo::client() const
{
    auto client = QString::fromStdString(m_nativeInfo.client).simplified(); // 去除首尾和冗余空格

    // 过滤清除所有不可见/不可打印字符，防止界面显示乱码
    erase_if(client, [](const QChar& c) { return !c.isPrint(); });

    return client;
}

// 从 Peer ID（节点唯一标识符）中解析并猜测客户端名称
QString PeerInfo::peerIdClient() const
{
    // 如果 Peer ID 全是 0，说明还没拿到对方的 ID，此时不生成字符串，直接返回空
    if (m_nativeInfo.pid.is_all_zeros())
        return {};

    QString result;

    // 典型 BT 客户端的标识信息（比如 -qB4600-）存放在 Peer ID 的前 8 个字符内
    for (int i = 0; i < 8; ++i)
    {
        const std::uint8_t c = m_nativeInfo.pid[i];

        // 确保切片全是可打印的 ASCII 字符（32 到 126 之间），否则通常是异常 ID，返回“未知”
        if ((c < 32) || (c > 126))
            return tr("Unknown");

        result += QChar::fromLatin1(c);
    }

    return result;
}

// 获取该节点的下载进度（0.0 到 1.0 之间）
qreal PeerInfo::progress() const
{
    return m_nativeInfo.progress;
}

// 获取该节点向本地发送数据的流速（本地的下载速度，字节/秒）
int PeerInfo::payloadUpSpeed() const
{
    return m_nativeInfo.payload_up_speed;
}

// 获取该节点从本地接收数据的流速（本地的上传速度，字节/秒）
int PeerInfo::payloadDownSpeed() const
{
    return m_nativeInfo.payload_down_speed;
}

// 获取本地向该节点累计上传的总字节数
qlonglong PeerInfo::totalUpload() const
{
    return m_nativeInfo.total_upload;
}

// 获取本地从该节点累计下载的总字节数
qlonglong PeerInfo::totalDownload() const
{
    return m_nativeInfo.total_download;
}

// 将 libtorrent 内部维护的位图(bitfield)转换为 Qt 的 QBitArray，代表该节点拥有哪些分片
QBitArray PeerInfo::pieces() const
{
    return LT::toQBitArray(m_nativeInfo.pieces);
}

// 获取连接类型名称（显示在客户端列表上，如 "uTP", "BT", "Web"）
QString PeerInfo::connectionType() const
{
    if (m_nativeInfo.flags & lt::peer_info::utp_socket)
        return C_UTP; // 优先返回 uTP 连接

    return (m_nativeInfo.connection_type == lt::peer_info::standard_bittorrent)
        ? u"BT"_s    // 标准 BitTorrent 连接
        : u"Web"_s;   // WebSeed（通过 HTTP/HTTPS 的网页种子）
}

// 计算该节点对于本地下载的“关联度/实用价值”
qreal PeerInfo::calcRelevance(const QBitArray& allPieces) const
{
    const qsizetype localMissing = allPieces.count(false); // 本地还差多少个分片
    if (localMissing <= 0)
        return 0; // 本地已经下完了，该节点的价值归 0

    const QBitArray peerPieces = pieces();
    // 计算交集：对方拥有的、且本地目前正缺少的分片数量
    const qsizetype remoteHaves = (peerPieces & (~allPieces)).count(true);
    // 返回比例：对方能满足我们缺口的比例（0.0 ~ 1.0）
    return static_cast<qreal>(remoteHaves) / localMissing;
}

// 获取计算好的关联度
qreal PeerInfo::relevance() const
{
    return m_relevance;
}

// 核心状态机函数：根据 libtorrent 节点的标志位组合，翻译并拼装成常在界面里见到的状态字母（Flags，如 "dH", "U" 等）以及对应的鼠标悬停提示文本
void PeerInfo::determineFlags()
{
    // Lambda 内部辅助函数：用于向标志字符串追加字母，并向详细描述中追加对应释义行
    const auto updateFlags = [this](const QChar specifier, const QString& explanation)
        {
            m_flags += (specifier + u' ');
            m_flagsDescription += u"%1 = %2\n"_s.arg(specifier, explanation);
        };

    if (isInteresting())
    {
        if (isRemoteChocked())
        {
            // d = 本地客户端想下，但对方不给（感兴趣，但被阻塞）
            updateFlags(u'd', tr("Interested (local) and choked (peer)"));
        }
        else
        {
            // D = 正在从该节点下载（感兴趣，且对方未阻塞）
            updateFlags(u'D', tr("Interested (local) and unchoked (peer)"));
        }
    }

    if (isRemoteInterested())
    {
        if (isChocked())
        {
            // u = 对方想向本地要数据，但本地目前拒绝（对方感兴趣，但本地阻塞了它）
            updateFlags(u'u', tr("Interested (peer) and choked (local)"));
        }
        else
        {
            // U = 正在向该节点上传（对方感兴趣，且本地未阻塞它）
            updateFlags(u'U', tr("Interested (peer) and unchoked (local)"));
        }
    }

    // K = 对方给本地解除了阻塞，但本地对它不感兴趣（没我们需要的数据）
    if (!isRemoteChocked() && !isInteresting())
        updateFlags(u'K', tr("Not interested (local) and unchoked (peer)"));

    // ? = 本地给对方解除了阻塞，但对方并不想跟我们要数据
    if (!isChocked() && !isRemoteInterested())
        updateFlags(u'?', tr("Not interested (peer) and unchoked (local)"));

    // O = 乐观解除阻塞（正在被列入随机照顾流速的节点中）
    if (optimisticUnchoke())
        updateFlags(u'O', tr("Optimistic unchoke"));

    // S = 节点已遭“冷落”（因为长期没响应等原因被暂时打入冷宫）
    if (isSnubbed())
        updateFlags(u'S', tr("Peer snubbed"));

    // I = 连接是由对方主动打进来的（Incoming Connection，即入站连接）
    if (!isLocalConnection())
        updateFlags(u'I', tr("Incoming connection"));

    // H = 该节点是通过 DHT 网络认识并连上的
    if (fromDHT())
        updateFlags(u'H', tr("Peer from DHT"));

    // X = 该节点是通过 PeX（节点交换）获得的
    if (fromPeX())
        updateFlags(u'X', tr("Peer from PEX"));

    // L = 该节点属于本地局域网发现的（LSD）
    if (fromLSD())
        updateFlags(u'L', tr("Peer from LSD"));

    // E = 该节点正在使用协议加密传输全部数据（全流量加密）
    if (isRC4Encrypted())
        updateFlags(u'E', tr("Encrypted traffic"));

    // e = 该节点仅在握手阶段使用了加密（后续数据是明文）
    if (isPlaintextEncrypted())
        updateFlags(u'e', tr("Encrypted handshake"));

    // P = 该节点正在使用 uTorrent 开发的 uTP 传输协议连接
    if (useUTPSocket())
        updateFlags(u'P', C_UTP);

    // h = 该节点使用了 NAT 穿透/打洞技术成功建立的连接
    if (isHolepunched())
        updateFlags(u'h', tr("Peer is using NAT hole punching"));

    // 剔除字符串末尾多余的一个空格（'\n' 或者是空格）
    m_flags.chop(1);
    m_flagsDescription.chop(1);
}

// 获取最终拼装好的简短状态标识符组合（例如："D I X P"）
QString PeerInfo::flags() const
{
    return m_flags;
}

// 获取格式化后的状态完整描述文档（多行文本，用于 GUI 的 Tooltip 悬停提示）
QString PeerInfo::flagsDescription() const
{
    return m_flagsDescription;
}

// 获取该节点目前正在下载（或请求）的区块索引号（Piece Index）
int PeerInfo::downloadingPieceIndex() const
{
    return static_cast<int>(m_nativeInfo.downloading_piece_index);
}


int PeerInfo::rtt() const {
    return m_nativeInfo.rtt;
}


int PeerInfo::peerRemoteDownSpeed() const {
    return m_nativeInfo.peer_remote_down_speed;
}

PeerInfo::~PeerInfo() = default;
