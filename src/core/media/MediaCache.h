#pragma once
// ses::core — MediaCache: the GUI-side owner of decoded PCM arenas
// (DESIGN.md §4.3). Buffers live here as std::shared_ptr<const AudioBuffer>;
// a RenderPlan holds raw pointers into them, so an arena may only be evicted
// after (a) nobody but the cache references it and (b) the reclaim grace has
// passed since its last use. Buffers are pinned while a project is open.
// GUI thread only — insertion comes from the decode pool's marshalled result,
// eviction is a deliberate GUI-side act. Qt6::Core only.

#include "core/media/AudioBuffer.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <chrono>
#include <memory>

namespace ses::core {

class MediaCache {
public:
    using Clock = std::chrono::steady_clock;
    using BufferPtr = std::shared_ptr<const AudioBuffer>;

    // Stores (or replaces) the arena under key and marks it used.
    BufferPtr insert(const QString& key, BufferPtr buffer);

    // Returns the arena and marks it used; nullptr when absent.
    BufferPtr find(const QString& key);

    bool contains(const QString& key) const { return entries_.contains(key); }
    int size() const { return entries_.size(); }
    QStringList keys() const { return entries_.keys(); }

    // Pinned buffers are never evicted (the project-open default).
    void pin(const QString& key);
    void unpin(const QString& key);
    void pinAll();
    void unpinAll();
    int pinnedCount() const;

    // Evicts entries that are (a) unpinned and referenced by nobody but the
    // cache and (b) idle for at least grace. Returns the number evicted.
    int evict(std::chrono::milliseconds grace, Clock::time_point now = Clock::now());

    // Drops every entry (project close). Arenas outlive the cache for any
    // holder that still references them — shared_ptr semantics.
    void clear() { entries_.clear(); }

private:
    struct Entry {
        BufferPtr buffer;
        bool pinned = false;
        Clock::time_point lastUse;
    };

    QHash<QString, Entry> entries_;
};

} // namespace ses::core