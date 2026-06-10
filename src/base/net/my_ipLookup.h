#pragma once

#include <QString>
#include <QHostAddress>
#include <QtEndian>
#include <QHash>          // 核心：引入 QHash
#include <shared_mutex>   // 依然需要读写锁保护

//struct PeerGeoInfo {
//    QString location;      // 城市/省份
//    QString organization;  // ASN/ISP
//    QString country_iso;   // 国家代码
//};

//
//PeerGeoInfo My_IpLookup(const QHostAddress &qAddr);


class MaxMindDatabase;
struct PeerGeoInfo;

class GeoQueryService {
private:
    std::unique_ptr<MaxMindDatabase> asnDB;
    std::unique_ptr<MaxMindDatabase> cityDB;
    std::unique_ptr<MaxMindDatabase> cnDB;

    std::unordered_map<int, std::string> divisionShort;
    std::unordered_map<unsigned int, std::string> asnInfo;

    bool isChineseLocale;

    // ----- 使用 QHash 的缓存配置 (依然需要锁) -----
    mutable std::shared_mutex cacheMutex;
    mutable QHash<QHostAddress, PeerGeoInfo> geoCache; // 直接使用 QHash，极其简洁
    const int MAX_CACHE_SIZE = 200000;                  // 限制缓存上限


    void loadASNInfo(const std::string& path);

    void loadDivisionCode(const std::string& path, const std::string& sep);

    void resolveDivision(uint32_t code, std::vector<std::string_view>& short_names) const;

public:
    GeoQueryService(const std::string& asnPath, const std::string& cityPath, const std::string& cnPath,
        const std::string& asnInfoPath, const std::string& divShortPath);

    // 显式声明析构函数，防止 unique_ptr 触发不完整类型错误
    ~GeoQueryService();

    PeerGeoInfo queryFinalInfo(const QHostAddress& qAddr) const;
};
