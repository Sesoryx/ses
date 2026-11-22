#pragma once
// ses::engine — RT allocation guard hooks (DESIGN §13).
//
// The audio callback is not allowed to allocate (DESIGN §2/§4). A test-only
// malloc interposer (tests/rt_alloc_interposer.c, loaded via LD_PRELOAD) counts
// allocations made on the RT thread while this scope is armed. The hooks are
// weak: normal builds link them as null and pay only two predictable branches.
//
// Interposing at malloc (not operator new) also covers allocations made inside
// dynamically linked libraries on the RT path, notably soxr_process.

#if defined(__GNUC__) || defined(__clang__)
extern "C" {
void ses_rt_alloc_enter(void) __attribute__((weak));
void ses_rt_alloc_leave(void) __attribute__((weak));
}
#define SES_HAS_RT_ALLOC_HOOKS 1
#endif

namespace ses::engine {

// Arms allocation counting for the current thread for its lifetime. Never
// allocates, locks, or throws.
class RtAllocationScope {
public:
    RtAllocationScope() noexcept
    {
#ifdef SES_HAS_RT_ALLOC_HOOKS
        if (ses_rt_alloc_enter)
            ses_rt_alloc_enter();
#endif
    }

    ~RtAllocationScope() noexcept
    {
#ifdef SES_HAS_RT_ALLOC_HOOKS
        if (ses_rt_alloc_leave)
            ses_rt_alloc_leave();
#endif
    }

    RtAllocationScope(const RtAllocationScope&) = delete;
    RtAllocationScope& operator=(const RtAllocationScope&) = delete;
};

} // namespace ses::engine

#ifdef SES_HAS_RT_ALLOC_HOOKS
#undef SES_HAS_RT_ALLOC_HOOKS
#endif
