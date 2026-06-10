
#include "torrent.h"

#include <limits>

#include <QHash>

#include "infohash.h"



namespace BitTorrent
{
    std::size_t qHash(const TorrentState key, const std::size_t seed)
    {
        return ::qHash(static_cast<std::underlying_type_t<TorrentState>>(key), seed);
    }

    // Torrent

    const qreal Torrent::MAX_RATIO = std::numeric_limits<qreal>::infinity();

    TorrentID Torrent::id() const
    {
        return infoHash().toTorrentID();
    }

    bool Torrent::isRunning() const
    {
        return !isStopped();
    }

    qlonglong Torrent::remainingSize() const
    {
        return wantedSize() - completedSize();
    }

    void Torrent::toggleSequentialDownload()
    {
        setSequentialDownload(!isSequentialDownload());
    }

    void Torrent::toggleFirstLastPiecePriority()
    {
        setFirstLastPiecePriority(!hasFirstLastPiecePriority());
    }
}
