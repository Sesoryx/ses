#include "core/media/MediaCache.h"

namespace ses::core {

MediaCache::BufferPtr MediaCache::insert(const QString& key, BufferPtr buffer)
{
    Entry entry;
    entry.buffer = std::move(buffer);
    entry.lastUse = Clock::now();
    // Keep the pin state of an existing entry when replacing its arena.
    const auto it = entries_.constFind(key);
    if (it != entries_.constEnd())
        entry.pinned = it->pinned;
    entries_.insert(key, std::move(entry));
    return entries_[key].buffer;
}

MediaCache::BufferPtr MediaCache::find(const QString& key)
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
        return {};
    it->lastUse = Clock::now();
    return it->buffer;
}

void MediaCache::pin(const QString& key)
{
    const auto it = entries_.find(key);
    if (it != entries_.end())
        it->pinned = true;
}

void MediaCache::unpin(const QString& key)
{
    const auto it = entries_.find(key);
    if (it != entries_.end())
        it->pinned = false;
}

void MediaCache::pinAll()
{
    for (auto& entry : entries_)
        entry.pinned = true;
}

void MediaCache::unpinAll()
{
    for (auto& entry : entries_)
        entry.pinned = false;
}

int MediaCache::pinnedCount() const
{
    int n = 0;
    for (const auto& entry : entries_)
        n += entry.pinned ? 1 : 0;
    return n;
}

int MediaCache::evict(std::chrono::milliseconds grace, Clock::time_point now)
{
    int evicted = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        // (a) the cache holds the only reference — no live plan/holder — and
        // (b) the reclaim grace has passed since the last use.
        const bool unreferenced = it->buffer.use_count() == 1;
        const bool idle = now - it->lastUse >= grace;
        if (!it->pinned && unreferenced && idle) {
            it = entries_.erase(it);
            ++evicted;
        } else {
            ++it;
        }
    }
    return evicted;
}

} // namespace ses::core