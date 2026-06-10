#include "my_calPeerDownloadRate.h" // 请替换为你实际的头文件名

PeerDownloadSpeedEstimator::PeerDownloadSpeedEstimator(uint64_t piece_size, uint64_t full_size, int window_size)
    : m_window_size_seconds(window_size), m_piece_size(piece_size), m_full_size(full_size) {}

// 每次获取到 Peer 信息时调用此方法更新
void PeerDownloadSpeedEstimator::update(int current_piece_count, float progress) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::steady_clock::now();

    // 1. 分别计算由 piece_count 和 progress 得出的持有字节数
    uint64_t bytes_from_pieces = static_cast<uint64_t>(current_piece_count) * m_piece_size;
    uint64_t bytes_from_progress = static_cast<uint64_t>(progress * m_full_size);

    // 2. 取两者中更大的作为当前对方真实的持有文件总数
    uint64_t current_total_bytes = std::max(bytes_from_pieces, bytes_from_progress);

    // 3. 如果是第一次记录，或者算出的总字节数有实质性增加，压入队列
    if (m_history.empty() || current_total_bytes > m_history.back().total_bytes) {
        m_history.push_back({ now, current_piece_count, progress, current_total_bytes });
    }

    // 4. 清理滑动窗口之外的过期事件
    prune(now);
}

// 获取当前估算的下载速度（Bytes/s）
double PeerDownloadSpeedEstimator::getSpeedBytesPerSec() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::steady_clock::now();

    prune(now);

    // 如果队列里只有 1 个事件（比如刚连上，或者很久没下新区块了），速度为 0
    if (m_history.size() < 4) {
        return 0.0;
    }

    auto oldest = m_history.front();
    auto newest = m_history.back();

    // 计算时间差（秒）
    std::chrono::duration<double> elapsed = newest.timestamp - oldest.timestamp;

    // 防止除以 0
    if (elapsed.count() <= 0.0) return 0.0;

    // 直接使用最新和最老的“最大持有总数”之差作为这期间的下载量
    uint64_t downloaded_bytes = newest.total_bytes - oldest.total_bytes;

    // 速度 = 下载字节数 / 经过的时间
    return static_cast<double>(downloaded_bytes) / elapsed.count();
}

void PeerDownloadSpeedEstimator::prune(std::chrono::steady_clock::time_point now) {
    // 保持队列中至少有一个基准点。
    // 如果最老的事件超出了滑动窗口范围，则将其弹出。
    while (m_history.size() > 0) {
        std::chrono::duration<double> age = now - m_history.front().timestamp;
        if (age.count() > m_window_size_seconds) {
            m_history.pop_front();
        }
        else {
            break;
        }
    }

    //// 边缘情况：如果唯一剩下的基准点也已经非常老（例如 30 秒没动静了），
    //// 说明对方早就停止下载了，我们直接清空，让速度归零。
    //if (m_history.size() == 1) {
    //    std::chrono::duration<double> age = now - m_history.front().timestamp;
    //    if (age.count() > m_window_size_seconds) {
    //        // 更新基准点的时间戳到现在，避免后续下载时计算出巨大的 elapsed time
    //        m_history.front().timestamp = now;
    //    }
    //}
}
