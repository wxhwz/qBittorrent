#include <QtSystemDetection>   // Qt 系统检测头文件，用于识别操作系统类型

#include <chrono>              // 时间相关功能，如时间字面量
#include <cstdlib>             // 标准库函数，如 exit()
#include <memory>              // 智能指针，如 std::unique_ptr

#ifdef Q_OS_UNIX               // 如果是 Unix/Linux/macOS 系统
#include <sys/resource.h>      // 资源限制相关函数（如 getrlimit/setrlimit）
#endif

#ifndef Q_OS_WIN
#ifndef Q_OS_HAIKU
#include <unistd.h>            // POSIX 系统调用（如 daemon, isatty）
#endif // Q_OS_HAIKU
#elif defined DISABLE_GUI
#include <io.h>                // Windows 下控制台 I/O（如 _isatty）
#endif

#include <QCoreApplication>    // Qt 核心应用程序类
#include <QString>             // Qt 字符串类
#include <QThread>             // Qt 线程类（用于 msleep）

#ifndef DISABLE_GUI            // 如果启用了 GUI（非纯命令行模式）
// GUI-only includes
#include <QFont>               // 字体
#include <QMessageBox>         // 消息对话框
#include <QPainter>            // 绘图
#include <QPen>                // 画笔
#include <QSplashScreen>       // 启动画面
#include <QTimer>              // 定时器

#ifdef QBT_STATIC_QT           // 静态链接 Qt 时的额外插件导入
#include <QtPlugin>
Q_IMPORT_PLUGIN(QICOPlugin)    // 导入 Windows ICO 图标插件
#endif // QBT_STATIC_QT

#else // DISABLE_GUI
#include <cstdio>              // 纯命令行模式下使用标准 I/O
#endif // DISABLE_GUI

#include "base/global.h"       // 全局定义
#include "base/logger.h"       // 日志系统
#include "base/preferences.h"  // 偏好设置
#include "base/profile.h"      // 配置文件管理
#include "base/settingvalue.h" // 缓存配置值
#include "base/version.h"      // 版本信息
#include "application.h"       // 自定义 Application 类
#include "cmdoptions.h"        // 命令行参数解析
#include "legalnotice.h"       // 法律声明显示
#include "signalhandler.h"     // 信号处理器（如 SIGINT）

#ifndef DISABLE_GUI
#include "gui/utils.h"         // GUI 辅助函数（如屏幕居中）
#endif

using namespace std::chrono_literals;   // 使用 chrono 时间字面量（如 1500ms）

namespace
{
    // 显示错误的命令行参数信息
    void displayBadArgMessage(const QString& message)
    {
        const QString help = QCoreApplication::translate("Main", "Run application with -h option to read about command line parameters.");
#if defined(Q_OS_WIN) && !defined(DISABLE_GUI)   // Windows GUI 模式下用消息框显示
        QMessageBox msgBox(QMessageBox::Critical, QCoreApplication::translate("Main", "Bad command line"),
            (message + u'\n' + help), QMessageBox::Ok);
        msgBox.show();                           // 需要先显示才能移动到屏幕中央
        msgBox.move(Utils::Gui::screenCenter(&msgBox));
        msgBox.exec();
#else                                            // 其他平台或纯命令行模式输出到 stderr
        const QString errMsg = QCoreApplication::translate("Main", "Bad command line: ") + u'\n'
            + message + u'\n'
            + help + u'\n';
        fprintf(stderr, "%s", qUtf8Printable(errMsg));
#endif
    }

    // 显示不可恢复的错误信息
    void displayErrorMessage(const QString& message)
    {
#ifndef DISABLE_GUI
        if (QApplication::instance())            // 有 GUI 实例时用消息框
        {
            QMessageBox msgBox;
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setText(QCoreApplication::translate("Main", "An unrecoverable error occurred."));
            msgBox.setInformativeText(message);
            msgBox.show();                       // 需要先显示才能移动到屏幕中央
            msgBox.move(Utils::Gui::screenCenter(&msgBox));
            msgBox.exec();
        }
        else                                     // 没有 GUI 实例时输出到 stderr
        {
            const QString errMsg = QCoreApplication::translate("Main", "qBittorrent has encountered an unrecoverable error.") + u'\n' + message + u'\n';
            fprintf(stderr, "%s", qUtf8Printable(errMsg));
        }
#else
        // 纯命令行模式直接输出到 stderr
        const QString errMsg = QCoreApplication::translate("Main", "qBittorrent has encountered an unrecoverable error.") + u'\n' + message + u'\n';
        fprintf(stderr, "%s", qUtf8Printable(errMsg));
#endif
    }

#if !defined(Q_OS_WIN) || defined(DISABLE_GUI)
    // 显示版本信息（非 Windows 平台或纯命令行模式）
    void displayVersion()
    {
        printf("%s %s\n", qUtf8Printable(qApp->applicationName()), QBT_VERSION);
    }
#endif

#ifndef DISABLE_GUI
    // 显示启动画面（仅在 GUI 模式）
    void showSplashScreen()
    {
        QPixmap splashImg(u":/icons/splash.png"_s);        // 加载启动画面图片
        QPainter painter(&splashImg);
        const auto version = QStringLiteral(QBT_VERSION); // 获取版本号字符串
        painter.setPen(QPen(Qt::white));
        painter.setFont(QFont(u"Arial"_s, 22, QFont::Black));
        painter.drawText(224 - painter.fontMetrics().horizontalAdvance(version), 270, version);  // 在图片上绘制版本号
        QSplashScreen* splash = new QSplashScreen(splashImg);
        splash->show();
        QTimer::singleShot(1500ms, Qt::CoarseTimer, splash, &QObject::deleteLater);  // 1.5 秒后自动销毁启动画面
        qApp->processEvents();                              // 立即处理事件，确保画面显示
    }
#endif  // DISABLE_GUI

#ifdef Q_OS_UNIX
    // 调整文件描述符限制，将其提升到系统允许的最大值
    void adjustFileDescriptorLimit()
    {
        rlimit limit{};

        if (getrlimit(RLIMIT_NOFILE, &limit) != 0)   // 获取当前限制
            return;

        limit.rlim_cur = limit.rlim_max;             // 将软限制设为硬限制（最大值）
        setrlimit(RLIMIT_NOFILE, &limit);            // 应用新限制
    }

    // 调整区域设置（locale），如果用户未设置 LANG 环境变量，则默认设为 C.UTF-8
    void adjustLocale()
    {
        // 指定默认 locale，以防用户没有安装任何 locale 包时只有 "C" locale 可用
        if (qEnvironmentVariableIsEmpty("LANG"))
            qputenv("LANG", "C.UTF-8");
    }
#endif
}

// 主函数
int main(int argc, char* argv[])
{
#ifdef DISABLE_GUI
    setvbuf(stdout, nullptr, _IONBF, 0);   // 纯命令行模式下禁用 stdout 缓冲，确保日志实时输出
#endif

#ifdef Q_OS_UNIX
    adjustLocale();                       // 调整 locale 设置（Unix 系统）
    adjustFileDescriptorLimit();          // 提升文件描述符限制
#endif

    // `app` 必须在 try 块外部声明，以便在异常发生时仍能显示错误消息框
    std::unique_ptr<Application> app;
    try
    {
        // 创建 Application 对象（该对象会解析命令行参数并初始化 Qt 应用）
        app = std::make_unique<Application>(argc, argv);

#ifdef Q_OS_WIN
        // QCoreApplication::applicationDirPath() 需要先实例化 Application 对象
        // 设置环境变量 _NT_SYMBOL_PATH，用于调试符号路径（Windows）
        const char envName[] = "_NT_SYMBOL_PATH";
        const QString envValue = qEnvironmentVariable(envName);
        if (envValue.isEmpty())
            qputenv(envName, Application::applicationDirPath().toLocal8Bit());
        else
            qputenv(envName, u"%1;%2"_s.arg(envValue, Application::applicationDirPath()).toLocal8Bit());
#endif

        const QBtCommandLineParameters params = app->commandLineArgs();  // 获取解析后的命令行参数

        // “显示帮助/版本”具有最高优先级
        if (params.showHelp)
        {
            displayUsage(QString::fromLocal8Bit(argv[0]));  // 显示用法帮助
            return EXIT_SUCCESS;
        }
#if !defined(Q_OS_WIN) || defined(DISABLE_GUI)
        if (params.showVersion)
        {
            displayVersion();                               // 显示版本信息
            return EXIT_SUCCESS;
        }
#endif

        // 如果存在未知的命令行参数，抛出异常
        if (!params.unknownParameter.isEmpty())
        {
            throw CommandLineParameterError(QCoreApplication::translate("Main", "%1 is an unknown command line parameter.",
                "--random-parameter is an unknown command line parameter.")
                .arg(params.unknownParameter));
        }

        // 检查是否已经有另一个 qBittorrent 实例在运行
        if (app->hasAnotherInstance())
        {
#if defined(DISABLE_GUI) && !defined(Q_OS_WIN)
            // 纯命令行模式下，如果试图守护化（后台运行）但已有实例，则报错
            if (params.shouldDaemonize)
            {
                throw CommandLineParameterError(QCoreApplication::translate("Main", "You cannot use %1: qBittorrent is already running.")
                    .arg(u"-d (or --daemon)"_s));
            }

            // 如果没有其他命令行参数，打印友好提示信息
            if (argc == 1)
            {
                const QString message = QCoreApplication::translate("Main", "Another qBittorrent instance is already running.");
                printf("%s\n", qUtf8Printable(message));
            }
#endif

            QThread::msleep(300);          // 短暂等待，让已有实例处理传入的消息
            app->callMainInstance();       // 将参数传递给已有实例（例如打开 torrent 文件）
            return EXIT_SUCCESS;
        }

        // 缓存的法律声明接受状态，默认未接受
        CachedSettingValue<bool> legalNoticeShown{ u"LegalNotice/Accepted"_s, false };
        if (params.confirmLegalNotice)     // 如果命令行明确要求接受法律声明
            legalNoticeShown = true;

        // 如果尚未接受法律声明，则显示法律声明
        if (!legalNoticeShown)
        {
#ifndef DISABLE_GUI
            const bool isInteractive = true;                    // GUI 模式下总是交互式的
#elif defined(Q_OS_WIN)
            // Windows 纯命令行模式下判断 stdin/stdout 是否为终端
            const bool isInteractive = (_isatty(_fileno(stdin)) != 0) && (_isatty(_fileno(stdout)) != 0);
#else
            // 其他 Unix 系统：守护模式下非交互式，只有终端下才显示法律声明
            const bool isInteractive = !params.shouldDaemonize
                && ((isatty(fileno(stdin)) != 0) && (isatty(fileno(stdout)) != 0));
#endif
            showLegalNotice(isInteractive);                     // 显示法律声明
            if (isInteractive)
                legalNoticeShown = true;                        // 交互模式下标记为已接受
        }

#ifdef Q_OS_MACOS
        // macOS 上用户设置 PATH 较困难，这里为方便添加 Homebrew Python 路径（用于搜索功能）
        const QByteArray path = "/usr/local/bin:" + qgetenv("PATH");
        qputenv("PATH", path.constData());

        // macOS 上默认不在菜单中显示图标
        app->setAttribute(Qt::AA_DontShowIconsInMenus);
#else
        // 其他平台根据偏好设置决定是否在菜单中显示图标
        if (!Preferences::instance()->iconsInMenusEnabled())
            app->setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

#if defined(DISABLE_GUI) && !defined(Q_OS_WIN)
        // 纯命令行模式且要求守护化（后台运行）
        if (params.shouldDaemonize)
        {
            app.reset();                                         // 先销毁当前 Application 对象
            if (::daemon(1, 0) == 0)                            // 调用 Unix daemon 函数，后台运行
            {
                app = std::make_unique<Application>(argc, argv); // 重新创建 Application
                if (app->hasAnotherInstance())
                {
                    // 理论上不应出现另一个实例，若出现则记录错误并退出
                    const QString errorMessage = QCoreApplication::translate("Main", "Found unexpected qBittorrent instance. Exiting this instance. Current process ID: %1.")
                        .arg(QString::number(QCoreApplication::applicationPid()));
                    LogMsg(errorMessage, Log::CRITICAL);
                    // stdout/stderr 已关闭，无法输出到终端
                    return EXIT_FAILURE;
                }
            }
            else
            {
                // 守护化失败
                const QString errorMessage = QCoreApplication::translate("Main", "Error when daemonizing. Reason: \"%1\". Error code: %2.")
                    .arg(QString::fromLocal8Bit(strerror(errno)), QString::number(errno));
                LogMsg(errorMessage, Log::CRITICAL);
                qCritical("%s", qUtf8Printable(errorMessage));
                return EXIT_FAILURE;
            }
        }
#elif !defined(DISABLE_GUI)
        // GUI 模式下，如果未禁用启动画面且偏好设置未禁用，则显示启动画面
        if (!(params.noSplash || Preferences::instance()->isSplashScreenDisabled()))
            showSplashScreen();
#endif

        registerSignalHandlers();        // 注册信号处理器（如 SIGINT、SIGTERM，实现优雅退出）

        return app->exec();              // 进入 Qt 事件循环，直到应用退出
    }
    catch (const CommandLineParameterError& er)   // 捕获命令行参数错误
    {
        displayBadArgMessage(er.message());
        return EXIT_FAILURE;
    }
    catch (const RuntimeError& er)                // 捕获运行时错误
    {
        displayErrorMessage(er.message());
        return EXIT_FAILURE;
    }
}
