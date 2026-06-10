#pragma once

#include <QStatusBar>

class QLabel;
class QPushButton;

namespace BitTorrent
{
    struct SessionStatus;
}

// 状态栏类，用于显示下载/上传速度、DHT 节点数、外部 IP、可用磁盘空间等信息
class StatusBar final : public QStatusBar
{
    Q_OBJECT
        Q_DISABLE_COPY_MOVE(StatusBar)  // 禁止拷贝和移动构造

public:
    explicit StatusBar(QWidget* parent = nullptr);
    ~StatusBar() override;

signals:
    void alternativeSpeedsButtonClicked();  // 点击“备用速度限制”按钮时发出的信号
    void connectionButtonClicked();         // 点击“连接状态”按钮时发出的信号

public slots:
    void showRestartRequired();  // 显示需要重启的提示

private slots:
    void refresh();               // 定时刷新状态栏信息
    void updateAltSpeedsBtn(bool alternative);  // 更新备用速度按钮的状态
    void capSpeed();             // 限制速度（槽函数，响应速度限制按钮点击）
    void optionsSaved();         // 选项保存后的处理

//my
private slots:
    void showExternalIpMenu(const QPoint& pos); // 新增：处理外部IP右键菜单的槽函数


private:
    void updateConnectionStatus();          // 更新连接状态（图标、提示文本）
    void updateDHTNodesNumber();            // 更新 DHT 节点数量显示
    void updateFreeDiskSpaceLabel(qint64 value);  // 更新可用磁盘空间标签
    void updateFreeDiskSpaceVisibility();   // 根据设置显示或隐藏可用磁盘空间标签
    void updateExternalAddressesLabel();    // 更新外部 IP 地址标签
    void updateExternalAddressesVisibility(); // 根据设置显示或隐藏外部 IP 标签
    void updateSpeedLabels();               // 更新下载/上传速度标签

    // UI 控件成员
    QPushButton* m_dlSpeedLbl = nullptr;           // 下载速度显示按钮，点击可限制速度
    QPushButton* m_upSpeedLbl = nullptr;           // 上传速度显示按钮，点击可限制速度
    QLabel* m_freeDiskSpaceLbl = nullptr;          // 可用磁盘空间标签
    QWidget* m_freeDiskSpaceSeparator = nullptr;   // 分隔线（用于布局）
    QLabel* m_lastExternalIPsLbl = nullptr;        // 外部 IP 地址标签
    QWidget* m_lastExternalIPsSeparator = nullptr; // 分隔线
    QLabel* m_DHTLbl = nullptr;                    // DHT 节点数量标签
    QWidget* m_DHTSeparator = nullptr;             // 分隔线
    QPushButton* m_connecStatusLblIcon = nullptr;  // 连接状态按钮（显示图标，点击可打开连接设置）
    QPushButton* m_altSpeedsBtn = nullptr;         // 备用速度限制按钮（切换常规/备用速度限制）
};
