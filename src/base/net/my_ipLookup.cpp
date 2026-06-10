#include "my_ipLookup.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>

#include <QLocale>


#include <maxminddb.h>

#include "geoipmanager.h"
#include "base/preferences.h"

//#pragma comment(lib, "maxminddb.lib")

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif



/**
 * @brief 将 QHostAddress 转换为通用的 sockaddr_storage
 * @param qAddr 输入的 QHostAddress (IPv4 或 IPv6)
 * @param addrStorage 输出的 sockaddr_storage 结构体指针
 * @return 转换成功返回 true，否则返回 false
 */
bool qHostAddressToSockAddr(const QHostAddress& qAddr, struct sockaddr_storage* addrStorage) {
    if (!addrStorage) return false;

    // 清空内存，避免未初始化的随机值
    *addrStorage = {};

    if (qAddr.protocol() == QAbstractSocket::IPv4Protocol) {
        struct sockaddr_in* sa_in = reinterpret_cast<struct sockaddr_in*>(addrStorage);
        sa_in->sin_family = AF_INET;

        // 注意：必须从 Host 字节序转换为 Network(大端) 字节序
        sa_in->sin_addr.s_addr = qToBigEndian<quint32>(qAddr.toIPv4Address());
        return true;

    }
    else if (qAddr.protocol() == QAbstractSocket::IPv6Protocol) {
        struct sockaddr_in6* sa_in6 = reinterpret_cast<struct sockaddr_in6*>(addrStorage);
        sa_in6->sin6_family = AF_INET6;

        // Q_IPV6ADDR 内部已经是一个 16 字节的数组 (c[16])，直接拷贝即可
        Q_IPV6ADDR ipv6 = qAddr.toIPv6Address();
        std::memcpy(sa_in6->sin6_addr.s6_addr, ipv6.c, sizeof(ipv6.c));
        return true;
    }

    return false; // 不支持的协议类型
}

class MaxMindDatabase {
private:
    MMDB_s db;
    bool isOpen = false;

public:
    MaxMindDatabase(const std::string& path) {
        if (MMDB_open(path.c_str(), MMDB_MODE_MMAP, &db) == MMDB_SUCCESS) {
            isOpen = true;
        }
        else {
            std::cerr << "Failed or missing database: " << path << std::endl;
        }
    }

    ~MaxMindDatabase() {
        if (isOpen) {
            MMDB_close(&db);
        }
    }

    MaxMindDatabase(const MaxMindDatabase&) = delete;
    MaxMindDatabase& operator=(const MaxMindDatabase&) = delete;

    bool isAvailable() const { return isOpen; }

    MMDB_lookup_result_s lookup(const QHostAddress& qAddr, int& gai_error, int& mmdb_error) {
        struct sockaddr_storage addr_storage;
        // 1. 将 QHostAddress 转换为 sockaddr_storage
        if (!qHostAddressToSockAddr(qAddr, &addr_storage)) {
            //qWarning() << "Invalid or unsupported IP address type";
            return MMDB_lookup_result_s{};
        }
        else {
            return MMDB_lookup_sockaddr(&db, reinterpret_cast<struct sockaddr*>(&addr_storage), &gai_error);
        }

    }

    std::string_view getString(MMDB_lookup_result_s* result, const char* const* path) const {
        MMDB_entry_data_s entry_data;
        int status = MMDB_aget_value(&result->entry, &entry_data, path);
        if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
            return std::string_view(entry_data.utf8_string, entry_data.data_size);
        }
        return "";
    }

    uint32_t getUint32(MMDB_lookup_result_s* result, const char* const* path) const {
        MMDB_entry_data_s entry_data;
        int status = MMDB_aget_value(&result->entry, &entry_data, path);
        if (status == MMDB_SUCCESS && entry_data.has_data) {
            if (entry_data.type == MMDB_DATA_TYPE_UINT32) return entry_data.uint32;
            if (entry_data.type == MMDB_DATA_TYPE_UINT16) return entry_data.uint16;
            if (entry_data.type == MMDB_DATA_TYPE_UINT64) return static_cast<uint32_t>(entry_data.uint64);
        }
        return 0;
    }
};

// ---------- 查询路径常量 ----------
namespace MMDBPaths {
    const char* const asn_num[] = { "autonomous_system_number", nullptr };
    const char* const asn_name[] = { "autonomous_system_organization", nullptr };
    const char* const country_iso[] = { "country", "iso_code", nullptr };
    const char* const country_zh[] = { "country", "names","cn", nullptr };
    const char* const country_en[] = { "country", "names","en", nullptr };
    const char* const city_zh[] = { "city", "cn", nullptr };
    const char* const city_en[] = { "city", "en", nullptr };
    const char* const sub_zh[] = { "subdivisions","cn", nullptr };
    const char* const sub_en[] = { "subdivisions","en", nullptr };
    const char* const div_code[] = { "division_code", nullptr };
    const char* const isp[] = { "isp", nullptr };
}

// ---------- 核心查询类 ----------
GeoQueryService::GeoQueryService(const std::string& asnPath, const std::string& cityPath, const std::string& cnPath,
    const std::string& asnInfoPath, const std::string& divShortPath)
{
    asnDB = std::make_unique<MaxMindDatabase>(asnPath);
    cityDB = std::make_unique<MaxMindDatabase>(cityPath);
    cnDB = std::make_unique<MaxMindDatabase>(cnPath);

    loadASNInfo(asnInfoPath);
    loadDivisionCode(divShortPath, "\t");
    QString locale = Preferences::instance()->getLocale();

    // 如果配置为空，说明用户选择了“系统默认”，此时获取系统当前的语言
    if (locale.isEmpty()) {
        locale = QLocale::system().name();
    }
    isChineseLocale = locale.startsWith(QLatin1String("zh"), Qt::CaseInsensitive);
}

// 析构函数的实现必须在能看到 MaxMindDatabase 完整定义的 .cpp 文件中
GeoQueryService::~GeoQueryService() = default;

void GeoQueryService::loadASNInfo(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        std::size_t idx = line.find('\t');
        if (idx == std::string::npos) continue;
        try {
            unsigned int asn = std::stoul(line.substr(0, idx));
            asnInfo[asn] = line.substr(idx + 1);
        }
        catch (...) { continue; }
    }
}

void GeoQueryService::loadDivisionCode(const std::string& path, const std::string& sep)
{
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        std::size_t idx = line.find(sep);
        if (idx == std::string::npos) continue;
        try {
            int code = std::stoi(line.substr(0, idx));
            divisionShort[code] = line.substr(idx + sep.length());
        }
        catch (...) { continue; }
    }
}

void GeoQueryService::resolveDivision(uint32_t code, std::vector<std::string_view>& short_names) const {
    if (code == 0) return;
    int c = static_cast<int>(code);
    int provinceCode = (c / 10000) * 10000;
    int cityCode = (c / 100) * 100;

    //std::vector<std::string_view> short_names;
    short_names.reserve(3);

    static auto addIfNotPresent = [](std::string_view value, std::vector<std::string_view>& vec) {
        if (std::find(vec.begin(), vec.end(), value) == vec.end()) {
            vec.emplace_back(value);
        }
        };

    auto addIfExists = [&](int key, auto& vec) {
        auto it = divisionShort.find(key);
        if (it != divisionShort.end()) {
            addIfNotPresent(it->second, vec);
        }
        };

    addIfExists(provinceCode, short_names);
    if (cityCode != provinceCode) addIfExists(cityCode, short_names);
    if (c != cityCode && c != provinceCode) addIfExists(c, short_names);

}

PeerGeoInfo GeoQueryService::queryFinalInfo(const QHostAddress& qAddr) const {
    // ----------------- 步骤 1：读取 QHash 缓存 (加共享读锁) -----------------
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex);
        auto it = geoCache.constFind(qAddr);
        if (it != geoCache.constEnd()) {
            return it.value();
        }
    }

    int gai_error = 0, mmdb_error = 0;

    std::string_view country_iso_view;
    QString qLocation;
    QString qOrganization;

    // 1. ASN 查询
    if (asnDB->isAvailable()) {
        auto result = asnDB->lookup(qAddr, gai_error, mmdb_error);
        if (gai_error == 0 && result.found_entry) {
            uint32_t asn_num = asnDB->getUint32(&result, MMDBPaths::asn_num);
            if (asn_num != 0) {
                std::string_view org_view = asnDB->getString(&result, MMDBPaths::asn_name);
                qOrganization = QString::fromUtf8(org_view.data(), static_cast<int>(org_view.size()));
            }
        }
    }

    // 2. City 与 CN DB 联合查询
    if (cityDB->isAvailable()) {
        auto city_result = cityDB->lookup(qAddr, gai_error, mmdb_error);
        if (gai_error == 0 && city_result.found_entry) {
            // 先只提取国家 ISO，用于判断是否属于中国
            country_iso_view = cityDB->getString(&city_result, MMDBPaths::country_iso);

            bool location_resolved = false;

            // 【优化】如果确定是中国 IP，优先用 cnDB 解析 location
            if (country_iso_view == "CN" && cnDB->isAvailable()) {
                auto cn_result = cnDB->lookup(qAddr, gai_error, mmdb_error);
                if (gai_error == 0 && cn_result.found_entry) {
                    uint32_t divCode = cnDB->getUint32(&cn_result, MMDBPaths::div_code);
                    if (divCode != 0) {
                        std::vector<std::string_view> div;
                        resolveDivision(divCode, div);
                        if (!div.empty()) {
                            std::string division;
                            bool flag = false;
                            for (auto& sv : div) {
                                division.append(sv);
                                if (!flag) {
                                    division.append(" ");
                                    flag = true;
                                }
                            }
                            qLocation = QString::fromUtf8(division.data(), static_cast<int>(division.size()));
                            location_resolved = true; // 标记 cnDB 解析成功
                        }
                    }
                }
            }

            // 【回退逻辑】如果不是中国 IP，或者 cnDB 遗憾没查到，则回退使用 cityDB 详细解析
            if (!location_resolved) {
                std::string_view country_name_en = cityDB->getString(&city_result, MMDBPaths::country_en);

                // -- 处理省份/州 (Sub) --
                std::string_view sub_name_en = cityDB->getString(&city_result, MMDBPaths::sub_en);
                std::string_view sub_name = sub_name_en;

                if (isChineseLocale) {
                    std::string_view sub_name_zh = cityDB->getString(&city_result, MMDBPaths::sub_zh);
                    if (!sub_name_zh.empty()) sub_name = sub_name_zh;
                }

                if (!sub_name.empty() && country_name_en != sub_name_en) {
                    qLocation = QString::fromUtf8(sub_name.data(), static_cast<int>(sub_name.size()));
                }

                // -- 处理城市 (City) --
                std::string_view city_name_en = cityDB->getString(&city_result, MMDBPaths::city_en);
                std::string_view city_name = city_name_en;

                if (isChineseLocale) {
                    std::string_view city_name_zh = cityDB->getString(&city_result, MMDBPaths::city_zh);
                    if (!city_name_zh.empty()) city_name = city_name_zh;
                }

                if (!city_name.empty() && country_name_en != city_name_en && sub_name_en != city_name_en) {
                    if (qLocation.isEmpty()) {
                        qLocation = QString::fromUtf8(city_name.data(), static_cast<int>(city_name.size()));
                    }
                    else {
                        qLocation += u' ';
                        qLocation += QString::fromUtf8(city_name.data(), static_cast<int>(city_name.size()));
                    }
                }
            }
        }
    }

    // 聚合结果
    PeerGeoInfo info{
        qLocation,
        qOrganization,
        QString::fromUtf8(country_iso_view.data(), static_cast<int>(country_iso_view.size()))
    };

    // ----------------- 步骤 3：写入 QHash 缓存 (加独占写锁) -----------------
    {
        std::unique_lock<std::shared_mutex> lock(cacheMutex);
        if (geoCache.size() >= MAX_CACHE_SIZE) {
            auto it = geoCache.begin();
            int toRemove = MAX_CACHE_SIZE / 5;
            while (toRemove-- > 0 && it != geoCache.end()) {
                it = geoCache.erase(it);
            }
        }
        geoCache.insert(qAddr, info);
    }

    return info;
}
