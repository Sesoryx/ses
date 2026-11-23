#pragma once
// ses::engine — lock-free RenderPlan slot (DESIGN.md §4.3, ROADMAP P2).
//
// Single producer (GUI thread: build → publish), single consumer (the audio
// callback: acquire once per block). The audio thread only ever holds the
// pointer for one block, so a plan is safe to delete once `graceBlocks`
// callbacks have elapsed since it was retired. The GUI thread performs every
// allocation and free here — never the callback.
//
// Qt-free: the plan types are core POD/Qt-Core data.

#include "core/render/RenderPlan.h"

#include <atomic>
#include <cstddef>
#include <utility>

namespace ses::engine {

class RenderPlanSlot {
public:
    // Grace: a plan is reclaimed after `graceBlocks` further acquisitions
    // (the consumer holds a plan for at most one block). Default 4 blocks.
    explicit RenderPlanSlot(int graceBlocks = 4)
        : grace_(graceBlocks > 0 ? graceBlocks : 1)
    {
    }

    ~RenderPlanSlot()
    {
        // The GUI thread owns every plan; drop whatever is still queued.
        const core::RenderPlan* p = current_.load(std::memory_order_acquire);
        delete p;
        for (auto& r : retired_)
            delete r.plan;
    }

    RenderPlanSlot(const RenderPlanSlot&) = delete;
    RenderPlanSlot& operator=(const RenderPlanSlot&) = delete;

    // GUI thread only. `bufferRefs` is moved into the plan so the arenas stay
    // alive as long as any live/retired plan references them.
    void publish(core::RenderPlan* next)
    {
        const core::RenderPlan* old = current_.exchange(next, std::memory_order_acq_rel);
        if (old)
            retire(old);
    }

    // Audio thread only, once per block.
    const core::RenderPlan* acquire()
    {
        blocksSinceRetire_.fetch_add(1, std::memory_order_relaxed);
        return current_.load(std::memory_order_acquire);
    }

    // GUI thread, on a 30 Hz timer: frees plans no callback can still hold.
    void reclaim()
    {
        const std::size_t ready = blocksSinceRetire_.load(std::memory_order_relaxed);
        std::size_t kept = 0;
        for (std::size_t i = 0; i < retireCount_; ++i) {
            Retired& r = retired_[(retireHead_ + i) % kRetiredCapacity];
            if (ready >= r.retireBlock + grace_) {
                delete r.plan; // GUI thread: allocation/free is fine here
                r.plan = nullptr;
            } else {
                retired_[(retireHead_ + kept) % kRetiredCapacity] = r;
                ++kept;
            }
        }
        retireCount_ = kept;
        retireHead_ = 0;
    }

    const core::RenderPlan* current() const
    {
        return current_.load(std::memory_order_acquire);
    }

private:
    struct Retired {
        const core::RenderPlan* plan = nullptr;
        std::size_t retireBlock = 0;
    };

    void retire(const core::RenderPlan* plan)
    {
        if (retireCount_ == kRetiredCapacity) {
            // Bounded by the grace window in practice; dropping the oldest is
            // the only lock-free option and still happens on the GUI thread.
            delete retired_[retireHead_].plan;
            retireHead_ = (retireHead_ + 1) % kRetiredCapacity;
            --retireCount_;
        }
        retired_[(retireHead_ + retireCount_) % kRetiredCapacity] =
            Retired{ plan, blocksSinceRetire_.load(std::memory_order_relaxed) };
        ++retireCount_;
    }

    static constexpr std::size_t kRetiredCapacity = 64;

    std::atomic<const core::RenderPlan*> current_{nullptr};
    std::atomic<std::size_t> blocksSinceRetire_{0};
    Retired retired_[kRetiredCapacity]{};
    std::size_t retireHead_ = 0;
    std::size_t retireCount_ = 0;
    std::size_t grace_ = 4;
};

} // namespace ses::engine
