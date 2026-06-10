#pragma once
#include <deque>
#include <chrono>
#include <mutex>
#include <cstdint>
#include <algorithm> // 引入 std::max

/**
 * @brief 用于估算远程 Peer 下载速度的工具类
 * 采用滑动窗口算法计算在指定时间窗口内下载的平均速度
 */
class PeerDownloadSpeedEstimator {
private:
    struct PieceEvent {
        std::chrono::steady_clock::time_point timestamp;
        int piece_count;
        float progress;
        uint64_t total_bytes; // 记录当时计算出的最大持有字节数
    };

    std::deque<PieceEvent> m_history;
    std::mutex m_mutex;

    // 滑动窗口大小（秒）
    int m_window_size_seconds;
    // 该 Torrent 的区块大小（字节）
    uint64_t m_piece_size;
    // 该 Torrent 的总大小（字节）
    uint64_t m_full_size;

    /**
     * @brief 清理过期事件
     * @param now 当前时间点
     */
    void prune(std::chrono::steady_clock::time_point now);

public:
    /**
     * @brief 构造函数
     * @param piece_size 每个区块的大小（字节）
     * @param full_size  种子总大小（字节），用于根据 progress 计算持有量
     * @param window_size 统计窗口大小（秒），默认为 15 秒
     */
    PeerDownloadSpeedEstimator(uint64_t piece_size, uint64_t full_size, int window_size = 30);

    /**
     * @brief 更新当前的下载信息
     * @param current_piece_count 对方当前累计下载的区块数
     * @param progress 对方当前的下载进度 (0.0 ~ 1.0)
     */
    void update(int current_piece_count, float progress);

    /**
     * @brief 获取当前估算的下载速度
     * @return 速度值，单位为 Bytes/s
     */
    double getSpeedBytesPerSec();
};
