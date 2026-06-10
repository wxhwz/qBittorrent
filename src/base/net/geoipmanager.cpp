#include "geoipmanager.h"

#include <QDateTime>
#include <QHostAddress>
#include <QLocale>
#include <QFileInfo>
#include <QFile>       // 用于操作独立的时间记录文件
#include <QSysInfo>    // 新增：用于检测系统与架构
#include <algorithm>   // 用于 std::find
#include <array>

#include "base/global.h"
#include "base/logger.h"
#include "base/preferences.h"
#include "base/profile.h"
#include "base/utils/fs.h"
#include "base/utils/gzip.h"
#include "base/utils/io.h"
#include "downloadmanager.h"

#include "my_ipLookup.h"

struct GeoFileInfo
{
    QLatin1String url;
    QLatin1String filename;
};

// 保持原有的 5 个地理数据文件不变
constexpr std::array<GeoFileInfo, 5> GEO_FILES = { {
    {QLatin1String("https://raw.github.com/wxhwz/my_mmdb_custom/refs/heads/database/GeoLite2-ASN-Custom.mmdb"), QLatin1String("GeoLite2-ASN-Custom.mmdb")},
    {QLatin1String("https://raw.github.com/wxhwz/my_mmdb_custom/refs/heads/database/GeoLite2-City-Custom.mmdb"), QLatin1String("GeoLite2-City-Custom.mmdb")},
    {QLatin1String("https://github.com/ljxi/GeoCN/releases/download/v26.4.19/GeoCN.mmdb"), QLatin1String("GeoCN.mmdb")},
    {QLatin1String("https://raw.githubusercontent.com/ljxi/GeoCN/refs/heads/main/data/asn.txt"), QLatin1String("asn.txt")},
    {QLatin1String("https://raw.githubusercontent.com/wxhwz/my_mmdb_custom/refs/heads/main/short.txt"), QLatin1String("short.txt")}
} };

// 文件夹常量定义
//const QLatin1String GEODB_FOLDER("GeoDB");
//const QLatin1String NEXTTRACE_FOLDER("Nexttrace"); // 要求的 Nexttrace 目录

// 辅助匿名空间：负责获取动态系统与架构后缀
namespace {
    QDateTime getLastDownloadTime(const QString& filePathStr) {
        QFile file(filePathStr + QLatin1String(".dltime"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString txt = QString::fromUtf8(file.readAll()).trimmed();
            QDateTime dt = QDateTime::fromString(txt, Qt::ISODate);
            return dt;
        }
        QFileInfo info(filePathStr);
        return info.lastModified();
    }

    void setLastDownloadTime(const QString& filePathStr) {
        QFile file(filePathStr + QLatin1String(".dltime"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
            file.write(now.toUtf8());
        }
    }

    // 核心新增：根据系统和CPU架构自动匹配 nexttrace-tiny 的文件名与 GitHub 最新直链
    bool getNextTraceFileInfo(QString& outUrl, QString& outFilename) {
        QString osStr;
        QString archStr;
        QString extStr;

        // 1. 判断系统
#if defined(Q_OS_WIN)
        osStr = QStringLiteral("windows");
        extStr = QStringLiteral(".exe");
#elif defined(Q_OS_MACOS)
        osStr = QStringLiteral("darwin");
#elif defined(Q_OS_LINUX)
        osStr = QStringLiteral("linux");
#else
        return false; // 不支持的系统
#endif

        // 2. 判断架构 (匹配 nexttrace-tiny release 命名规范)
        QString currentArch = QSysInfo::currentCpuArchitecture();
        if (currentArch == QStringLiteral("x86_64") || currentArch == QStringLiteral("amd64")) {
            archStr = QStringLiteral("amd64");
        }
        else if (currentArch == QStringLiteral("i386") || currentArch == QStringLiteral("i686")) {
            archStr = QStringLiteral("386");
        }
        else if (currentArch == QStringLiteral("arm64") || currentArch == QStringLiteral("aarch64")) {
            archStr = QStringLiteral("arm64");
        }
        else if (currentArch.startsWith(QStringLiteral("arm"))) {
            archStr = QStringLiteral("arm");
        }
        else {
            return false; // 不支持的架构
        }

        // 组合规范：nexttrace-tiny_系统的_架构(带后缀) 
        // 示例：nexttrace-tiny_windows_amd64.exe  或  nexttrace-tiny_linux_arm64
        outFilename = QStringLiteral("nexttrace-tiny_%1_%2%3").arg(osStr, archStr, extStr);
        // GitHub 最新发布直链（免去解析 API 的复杂逻辑）
        outUrl = QStringLiteral("https://github.com/nxtrace/NTrace-core/releases/latest/download/") + outFilename;

        return true;
    }
}


namespace Net {
    GeoIPManager* GeoIPManager::m_instance = nullptr;

    GeoIPManager::GeoIPManager() : m_currentDownloadIndex(-1), m_nexttraceDownloading(false)
    {
        LogMsg(tr("[GeoIPManager] Constructor called."), Log::INFO);
        configure();
        connect(Preferences::instance(), &Preferences::changed, this, &GeoIPManager::configure);
    }

    GeoIPManager::~GeoIPManager() {}

    GeoIPManager* GeoIPManager::instance() { return m_instance; }

    void GeoIPManager::initInstance()
    {
        if (!m_instance) m_instance = new GeoIPManager;
    }

    void GeoIPManager::freeInstance()
    {
        delete m_instance;
        m_instance = nullptr;
    }

    // 核心修改：加载数据库时，顺便触发 Nexttrace 的检测更新
    void GeoIPManager::loadDatabase()
    {
        LogMsg(tr("[loadDatabase] Starting database load."), Log::INFO);
        m_geoQueryService.reset();

        const Path baseDir = specialFolderLocation(SpecialFolder::Data) / Path(GEODB_FOLDER);
        std::vector<int> neededFiles;
        neededFiles.reserve(GEO_FILES.size());
        QDateTime now = QDateTime::currentDateTime();

        for (int i = 0; i < static_cast<int>(GEO_FILES.size()); ++i)
        {
            Path filePath = baseDir / Path(GEO_FILES[i].filename);
            QString filePathStr = filePath.toString();
            bool exists = filePath.exists();

            if (!exists) {
                neededFiles.push_back(i);
                continue;
            }

            QDateTime lastDl = getLastDownloadTime(filePathStr);
            int daysOld = lastDl.daysTo(now);
            if (!lastDl.isValid() || daysOld > 30) {
                neededFiles.push_back(i);
            }
        }

        // 即使本地 GeoDB 文件齐全，我们也去异步检查并更新 Nexttrace
        checkAndUpdateNexttrace();

        if (neededFiles.empty())
        {
            std::string asnPath = (baseDir / Path(GEO_FILES[0].filename)).toString().toStdString();
            std::string cityPath = (baseDir / Path(GEO_FILES[1].filename)).toString().toStdString();
            std::string cnPath = (baseDir / Path(GEO_FILES[2].filename)).toString().toStdString();
            std::string asnInfoPath = (baseDir / Path(GEO_FILES[3].filename)).toString().toStdString();
            std::string divShortPath = (baseDir / Path(GEO_FILES[4].filename)).toString().toStdString();

            m_geoQueryService = std::make_unique<GeoQueryService>(asnPath, cityPath, cnPath, asnInfoPath, divShortPath);
            LogMsg(tr("Custom Advanced IP Geolocation service initialized successfully."), Log::INFO);
        }
        else
        {
            downloadSpecificFiles(neededFiles);
        }
    }

    // 新增：检测并触发 Nexttrace 下载
    void GeoIPManager::checkAndUpdateNexttrace()
    {
        if (m_nexttraceDownloading) return; // 已经在下载中，跳过

        QString ntUrl;
        QString ntFilename;
        if (!getNextTraceFileInfo(ntUrl, ntFilename)) {
            LogMsg(tr("[Nexttrace] Current OS or Architecture is not supported by nexttrace-tiny."), Log::WARNING);
            return;
        }

        const Path ntDir = specialFolderLocation(SpecialFolder::Data) / Path(NEXTTRACE_FOLDER);
        const Path ntPath = ntDir / Path(ntFilename);

        bool needDownload = false;
        if (!ntPath.exists()) {
            needDownload = true;
            LogMsg(tr("[Nexttrace] File does not exist, scheduling download: %1").arg(ntFilename), Log::INFO);
        }
        else {
            QDateTime lastDl = getLastDownloadTime(ntPath.toString());
            // 超过30天未更新则尝试拉取最新版本
            if (!lastDl.isValid() || lastDl.daysTo(QDateTime::currentDateTime()) > 30) {
                needDownload = true;
                LogMsg(tr("[Nexttrace] File expired, scheduling update: %1").arg(ntFilename), Log::INFO);
            }
        }

        if (needDownload) {
            m_nexttraceDownloading = true;
            LogMsg(tr("[Nexttrace] Requesting download from: %1").arg(ntUrl), Log::INFO);

            // 使用特定的回调槽函数 nexttraceDownloadFinished 来承接下载结果
            DownloadManager::instance()->download(
                { ntUrl }, Preferences::instance()->useProxyForGeneralPurposes(),
                this, &GeoIPManager::nexttraceDownloadFinished);
        }
        else {
            LogMsg(tr("[Nexttrace] Local version is up-to-date: %1").arg(ntFilename), Log::INFO);
        }
    }

    // 新增：Nexttrace 下载完成后的专用处理槽函数
    void GeoIPManager::nexttraceDownloadFinished(const DownloadResult& result)
    {
        m_nexttraceDownloading = false;

        QString ntUrl;
        QString ntFilename;
        if (!getNextTraceFileInfo(ntUrl, ntFilename)) return;

        if (result.status != DownloadStatus::Success) {
            LogMsg(tr("Failed to download Nexttrace: %1. Reason: %2. Url: %3.").arg(ntFilename, result.errorString, ntUrl), Log::WARNING);
            return;
        }

        const Path targetDir = specialFolderLocation(SpecialFolder::Data) / Path(NEXTTRACE_FOLDER);
        if (!targetDir.exists()) {
            Utils::Fs::mkpath(targetDir);
        }

        const Path savePath = targetDir / Path(ntFilename);
        const nonstd::expected<void, QString> saveResult = Utils::IO::saveToFile(savePath, result.data);

        if (saveResult) {
            setLastDownloadTime(savePath.toString());
            LogMsg(tr("Successfully updated nexttrace-tiny to: %1").arg(savePath.toString()), Log::INFO);

            // 如果是在 Linux / macOS 下，可能需要赋予其执行权限（可选）
#if !defined(Q_OS_WIN)
            QFile::setPermissions(savePath.toString(), QFile::ExeUser | QFile::ReadUser | QFile::WriteUser | QFile::ExeGroup | QFile::ReadGroup);
#endif
        }
        else {
            LogMsg(tr("Couldn't save downloaded Nexttrace file. Reason: %2").arg(saveResult.error()), Log::WARNING);
        }
    }

    void GeoIPManager::downloadSpecificFiles(const std::vector<int>& fileIndices)
    {
        if (m_currentDownloadIndex >= 0) return;

        m_pendingDownloads = fileIndices;
        if (m_pendingDownloads.empty()) return;

        m_currentDownloadIndex = 0;
        downloadNextFile();
    }

    void GeoIPManager::downloadNextFile()
    {
        if (m_currentDownloadIndex < 0 || m_currentDownloadIndex >= static_cast<int>(m_pendingDownloads.size()))
        {
            LogMsg(tr("All targeted IP geolocation files downloaded successfully. Reloading service..."), Log::INFO);
            m_currentDownloadIndex = -1;
            m_pendingDownloads.clear();
            loadDatabase();
            return;
        }

        int fileIdx = m_pendingDownloads[m_currentDownloadIndex];
        const QString curUrl = GEO_FILES[fileIdx].url;

        DownloadManager::instance()->download(
            { curUrl }, Preferences::instance()->useProxyForGeneralPurposes(),
            this, &GeoIPManager::downloadFinished);
    }

    PeerGeoInfo GeoIPManager::lookup(const QHostAddress& hostAddr) const
    {
        if (m_enabled && m_geoQueryService) {
            return m_geoQueryService->queryFinalInfo(hostAddr);
        }
        return {};
    }

    void GeoIPManager::configure()
    {
        const bool enabled = Preferences::instance()->resolvePeerCountries();
        if (m_enabled != enabled) {
            m_enabled = enabled;
            if (m_enabled && !m_geoQueryService) {
                loadDatabase();
            }
            else if (!m_enabled) {
                m_geoQueryService.reset();
            }
        }
    }

    // 单个文件下载完成后的回调槽函数
    void GeoIPManager::downloadFinished(const DownloadResult& result) {
        //LogMsg(tr("[downloadFinished] Called with status=%1, error=%2").arg(static_cast<int>(result.status), result.errorString), Log::INFO);
        // 安全越界检查
        if (m_currentDownloadIndex < 0 || m_currentDownloadIndex >= static_cast<int>(m_pendingDownloads.size())) {
            LogMsg(tr("[downloadFinished] Invalid download index or no pending downloads. currentDownloadIndex=%1, pending size=%2").arg(m_currentDownloadIndex).arg(m_pendingDownloads.size()), Log::WARNING);
            return;
        }
        int fileIdx = m_pendingDownloads[m_currentDownloadIndex];
        const QString filename = GEO_FILES[fileIdx].filename; // 优化 6：直接使用隐式转换创建 QString
        // 下载失败处理
        if (result.status != DownloadStatus::Success) {
            LogMsg(tr("Failed to download IP geo file: %1. Reason: %2")
                .arg(filename, result.errorString), Log::WARNING);

            // 如果出现失败，中止整个更新队列
            m_currentDownloadIndex = -1;
            m_pendingDownloads.clear();
            LogMsg(tr("[downloadFinished] Download queue aborted due to failure."), Log::WARNING);
            return;
        }

        // 确定存储目录（profile/GeoDB），如果目录不存在则创建
        const Path targetDir = specialFolderLocation(SpecialFolder::Data) / Path(GEODB_FOLDER);
        if (!targetDir.exists()) {
            LogMsg(tr("[downloadFinished] Target directory does not exist, creating: %1").arg(targetDir.toString()), Log::INFO);
            Utils::Fs::mkpath(targetDir);
        }

        // 构建保存文件的完整路径
        const Path savePath = targetDir / Path(GEO_FILES[fileIdx].filename);
        LogMsg(tr("[downloadFinished] Saving file to: %1, size=%2 bytes").arg(savePath.toString(), QString::number(result.data.size())), Log::INFO);
        // 将下载的数据写入文件
        const nonstd::expected<void, QString> saveResult = Utils::IO::saveToFile(savePath, result.data);

        if (saveResult)
        {
            // **核心：文件落地成功后，记录当前的绝对时间为“最后成功下载时间”**
            // 优化 7：直接传入转换好的 QString，减少临时对象生成
            setLastDownloadTime(savePath.toString());

            LogMsg(tr("[downloadFinished] Successfully saved file: %1").arg(filename), Log::INFO);
            // 写入成功：索引后移，继续下载下一个文件
            m_currentDownloadIndex++;
            downloadNextFile();
        }
        else
        {
            // 写入失败：记录错误并停止下载队列
            LogMsg(tr("Couldn't save downloaded file %1. Reason: %2")
                .arg(filename, saveResult.error()), Log::WARNING);
            m_currentDownloadIndex = -1;
            m_pendingDownloads.clear();
            LogMsg(tr("[downloadFinished] Download queue aborted due to save failure."), Log::WARNING);
        }
    }

    QString GeoIPManager::CountryName(const QString& countryISOCode)
    {
        static const QHash<QString, QString> countries =
        {
            // ISO 3166-1 alpha-2 codes
            // http://www.iso.org/iso/home/standards/country_codes/country_names_and_code_elements_txt-temp.htm

            // Officially assigned
            {u"AD"_s, tr("Andorra")},
            {u"AE"_s, tr("United Arab Emirates")},
            {u"AF"_s, tr("Afghanistan")},
            {u"AG"_s, tr("Antigua and Barbuda")},
            {u"AI"_s, tr("Anguilla")},
            {u"AL"_s, tr("Albania")},
            {u"AM"_s, tr("Armenia")},
            {u"AO"_s, tr("Angola")},
            {u"AQ"_s, tr("Antarctica")},
            {u"AR"_s, tr("Argentina")},
            {u"AS"_s, tr("American Samoa")},
            {u"AT"_s, tr("Austria")},
            {u"AU"_s, tr("Australia")},
            {u"AW"_s, tr("Aruba")},
            {u"AX"_s, tr("Aland Islands")},
            {u"AZ"_s, tr("Azerbaijan")},
            {u"BA"_s, tr("Bosnia and Herzegovina")},
            {u"BB"_s, tr("Barbados")},
            {u"BD"_s, tr("Bangladesh")},
            {u"BE"_s, tr("Belgium")},
            {u"BF"_s, tr("Burkina Faso")},
            {u"BG"_s, tr("Bulgaria")},
            {u"BH"_s, tr("Bahrain")},
            {u"BI"_s, tr("Burundi")},
            {u"BJ"_s, tr("Benin")},
            {u"BL"_s, tr("Saint Barthelemy")},
            {u"BM"_s, tr("Bermuda")},
            {u"BN"_s, tr("Brunei Darussalam")},
            {u"BO"_s, tr("Bolivia, Plurinational State of")},
            {u"BQ"_s, tr("Bonaire, Sint Eustatius and Saba")},
            {u"BR"_s, tr("Brazil")},
            {u"BS"_s, tr("Bahamas")},
            {u"BT"_s, tr("Bhutan")},
            {u"BV"_s, tr("Bouvet Island")},
            {u"BW"_s, tr("Botswana")},
            {u"BY"_s, tr("Belarus")},
            {u"BZ"_s, tr("Belize")},
            {u"CA"_s, tr("Canada")},
            {u"CC"_s, tr("Cocos (Keeling) Islands")},
            {u"CD"_s, tr("Congo, The Democratic Republic of the")},
            {u"CF"_s, tr("Central African Republic")},
            {u"CG"_s, tr("Congo")},
            {u"CH"_s, tr("Switzerland")},
            {u"CI"_s, tr("Cote d'Ivoire")},
            {u"CK"_s, tr("Cook Islands")},
            {u"CL"_s, tr("Chile")},
            {u"CM"_s, tr("Cameroon")},
            {u"CN"_s, tr("China")},
            {u"CO"_s, tr("Colombia")},
            {u"CR"_s, tr("Costa Rica")},
            {u"CU"_s, tr("Cuba")},
            {u"CV"_s, tr("Cape Verde")},
            {u"CW"_s, tr("Curacao")},
            {u"CX"_s, tr("Christmas Island")},
            {u"CY"_s, tr("Cyprus")},
            {u"CZ"_s, tr("Czech Republic")},
            {u"DE"_s, tr("Germany")},
            {u"DJ"_s, tr("Djibouti")},
            {u"DK"_s, tr("Denmark")},
            {u"DM"_s, tr("Dominica")},
            {u"DO"_s, tr("Dominican Republic")},
            {u"DZ"_s, tr("Algeria")},
            {u"EC"_s, tr("Ecuador")},
            {u"EE"_s, tr("Estonia")},
            {u"EG"_s, tr("Egypt")},
            {u"EH"_s, tr("Western Sahara")},
            {u"ER"_s, tr("Eritrea")},
            {u"ES"_s, tr("Spain")},
            {u"ET"_s, tr("Ethiopia")},
            {u"FI"_s, tr("Finland")},
            {u"FJ"_s, tr("Fiji")},
            {u"FK"_s, tr("Falkland Islands (Malvinas)")},
            {u"FM"_s, tr("Micronesia, Federated States of")},
            {u"FO"_s, tr("Faroe Islands")},
            {u"FR"_s, tr("France")},
            {u"GA"_s, tr("Gabon")},
            {u"GB"_s, tr("United Kingdom")},
            {u"GD"_s, tr("Grenada")},
            {u"GE"_s, tr("Georgia")},
            {u"GF"_s, tr("French Guiana")},
            {u"GG"_s, tr("Guernsey")},
            {u"GH"_s, tr("Ghana")},
            {u"GI"_s, tr("Gibraltar")},
            {u"GL"_s, tr("Greenland")},
            {u"GM"_s, tr("Gambia")},
            {u"GN"_s, tr("Guinea")},
            {u"GP"_s, tr("Guadeloupe")},
            {u"GQ"_s, tr("Equatorial Guinea")},
            {u"GR"_s, tr("Greece")},
            {u"GS"_s, tr("South Georgia and the South Sandwich Islands")},
            {u"GT"_s, tr("Guatemala")},
            {u"GU"_s, tr("Guam")},
            {u"GW"_s, tr("Guinea-Bissau")},
            {u"GY"_s, tr("Guyana")},
            {u"HK"_s, tr("Hong Kong")},
            {u"HM"_s, tr("Heard Island and McDonald Islands")},
            {u"HN"_s, tr("Honduras")},
            {u"HR"_s, tr("Croatia")},
            {u"HT"_s, tr("Haiti")},
            {u"HU"_s, tr("Hungary")},
            {u"ID"_s, tr("Indonesia")},
            {u"IE"_s, tr("Ireland")},
            {u"IL"_s, tr("Israel")},
            {u"IM"_s, tr("Isle of Man")},
            {u"IN"_s, tr("India")},
            {u"IO"_s, tr("British Indian Ocean Territory")},
            {u"IQ"_s, tr("Iraq")},
            {u"IR"_s, tr("Iran, Islamic Republic of")},
            {u"IS"_s, tr("Iceland")},
            {u"IT"_s, tr("Italy")},
            {u"JE"_s, tr("Jersey")},
            {u"JM"_s, tr("Jamaica")},
            {u"JO"_s, tr("Jordan")},
            {u"JP"_s, tr("Japan")},
            {u"KE"_s, tr("Kenya")},
            {u"KG"_s, tr("Kyrgyzstan")},
            {u"KH"_s, tr("Cambodia")},
            {u"KI"_s, tr("Kiribati")},
            {u"KM"_s, tr("Comoros")},
            {u"KN"_s, tr("Saint Kitts and Nevis")},
            {u"KP"_s, tr("Korea, Democratic People's Republic of")},
            {u"KR"_s, tr("Korea, Republic of")},
            {u"KW"_s, tr("Kuwait")},
            {u"KY"_s, tr("Cayman Islands")},
            {u"KZ"_s, tr("Kazakhstan")},
            {u"LA"_s, tr("Lao People's Democratic Republic")},
            {u"LB"_s, tr("Lebanon")},
            {u"LC"_s, tr("Saint Lucia")},
            {u"LI"_s, tr("Liechtenstein")},
            {u"LK"_s, tr("Sri Lanka")},
            {u"LR"_s, tr("Liberia")},
            {u"LS"_s, tr("Lesotho")},
            {u"LT"_s, tr("Lithuania")},
            {u"LU"_s, tr("Luxembourg")},
            {u"LV"_s, tr("Latvia")},
            {u"LY"_s, tr("Libya")},
            {u"MA"_s, tr("Morocco")},
            {u"MC"_s, tr("Monaco")},
            {u"MD"_s, tr("Moldova, Republic of")},
            {u"ME"_s, tr("Montenegro")},
            {u"MF"_s, tr("Saint Martin (French part)")},
            {u"MG"_s, tr("Madagascar")},
            {u"MH"_s, tr("Marshall Islands")},
            {u"MK"_s, tr("Macedonia, The Former Yugoslav Republic of")},
            {u"ML"_s, tr("Mali")},
            {u"MM"_s, tr("Myanmar")},
            {u"MN"_s, tr("Mongolia")},
            {u"MO"_s, tr("Macao")},
            {u"MP"_s, tr("Northern Mariana Islands")},
            {u"MQ"_s, tr("Martinique")},
            {u"MR"_s, tr("Mauritania")},
            {u"MS"_s, tr("Montserrat")},
            {u"MT"_s, tr("Malta")},
            {u"MU"_s, tr("Mauritius")},
            {u"MV"_s, tr("Maldives")},
            {u"MW"_s, tr("Malawi")},
            {u"MX"_s, tr("Mexico")},
            {u"MY"_s, tr("Malaysia")},
            {u"MZ"_s, tr("Mozambique")},
            {u"NA"_s, tr("Namibia")},
            {u"NC"_s, tr("New Caledonia")},
            {u"NE"_s, tr("Niger")},
            {u"NF"_s, tr("Norfolk Island")},
            {u"NG"_s, tr("Nigeria")},
            {u"NI"_s, tr("Nicaragua")},
            {u"NL"_s, tr("Netherlands")},
            {u"NO"_s, tr("Norway")},
            {u"NP"_s, tr("Nepal")},
            {u"NR"_s, tr("Nauru")},
            {u"NU"_s, tr("Niue")},
            {u"NZ"_s, tr("New Zealand")},
            {u"OM"_s, tr("Oman")},
            {u"PA"_s, tr("Panama")},
            {u"PE"_s, tr("Peru")},
            {u"PF"_s, tr("French Polynesia")},
            {u"PG"_s, tr("Papua New Guinea")},
            {u"PH"_s, tr("Philippines")},
            {u"PK"_s, tr("Pakistan")},
            {u"PL"_s, tr("Poland")},
            {u"PM"_s, tr("Saint Pierre and Miquelon")},
            {u"PN"_s, tr("Pitcairn")},
            {u"PR"_s, tr("Puerto Rico")},
            {u"PS"_s, tr("Palestine, State of")},
            {u"PT"_s, tr("Portugal")},
            {u"PW"_s, tr("Palau")},
            {u"PY"_s, tr("Paraguay")},
            {u"QA"_s, tr("Qatar")},
            {u"RE"_s, tr("Reunion")},
            {u"RO"_s, tr("Romania")},
            {u"RS"_s, tr("Serbia")},
            {u"RU"_s, tr("Russian Federation")},
            {u"RW"_s, tr("Rwanda")},
            {u"SA"_s, tr("Saudi Arabia")},
            {u"SB"_s, tr("Solomon Islands")},
            {u"SC"_s, tr("Seychelles")},
            {u"SD"_s, tr("Sudan")},
            {u"SE"_s, tr("Sweden")},
            {u"SG"_s, tr("Singapore")},
            {u"SH"_s, tr("Saint Helena, Ascension and Tristan da Cunha")},
            {u"SI"_s, tr("Slovenia")},
            {u"SJ"_s, tr("Svalbard and Jan Mayen")},
            {u"SK"_s, tr("Slovakia")},
            {u"SL"_s, tr("Sierra Leone")},
            {u"SM"_s, tr("San Marino")},
            {u"SN"_s, tr("Senegal")},
            {u"SO"_s, tr("Somalia")},
            {u"SR"_s, tr("Suriname")},
            {u"SS"_s, tr("South Sudan")},
            {u"ST"_s, tr("Sao Tome and Principe")},
            {u"SV"_s, tr("El Salvador")},
            {u"SX"_s, tr("Sint Maarten (Dutch part)")},
            {u"SY"_s, tr("Syrian Arab Republic")},
            {u"SZ"_s, tr("Swaziland")},
            {u"TC"_s, tr("Turks and Caicos Islands")},
            {u"TD"_s, tr("Chad")},
            {u"TF"_s, tr("French Southern Territories")},
            {u"TG"_s, tr("Togo")},
            {u"TH"_s, tr("Thailand")},
            {u"TJ"_s, tr("Tajikistan")},
            {u"TK"_s, tr("Tokelau")},
            {u"TL"_s, tr("Timor-Leste")},
            {u"TM"_s, tr("Turkmenistan")},
            {u"TN"_s, tr("Tunisia")},
            {u"TO"_s, tr("Tonga")},
            {u"TR"_s, tr("Turkey")},
            {u"TT"_s, tr("Trinidad and Tobago")},
            {u"TV"_s, tr("Tuvalu")},
            {u"TW"_s, tr("Taiwan")},
            {u"TZ"_s, tr("Tanzania, United Republic of")},
            {u"UA"_s, tr("Ukraine")},
            {u"UG"_s, tr("Uganda")},
            {u"UM"_s, tr("United States Minor Outlying Islands")},
            {u"US"_s, tr("United States")},
            {u"UY"_s, tr("Uruguay")},
            {u"UZ"_s, tr("Uzbekistan")},
            {u"VA"_s, tr("Holy See (Vatican City State)")},
            {u"VC"_s, tr("Saint Vincent and the Grenadines")},
            {u"VE"_s, tr("Venezuela, Bolivarian Republic of")},
            {u"VG"_s, tr("Virgin Islands, British")},
            {u"VI"_s, tr("Virgin Islands, U.S.")},
            {u"VN"_s, tr("Vietnam")},
            {u"VU"_s, tr("Vanuatu")},
            {u"WF"_s, tr("Wallis and Futuna")},
            {u"WS"_s, tr("Samoa")},
            {u"YE"_s, tr("Yemen")},
            {u"YT"_s, tr("Mayotte")},
            {u"ZA"_s, tr("South Africa")},
            {u"ZM"_s, tr("Zambia")},
            {u"ZW"_s, tr("Zimbabwe")},

            {{}, tr("N/A")}
        };

        return countries.value(countryISOCode, tr("N/A"));
    }
}




