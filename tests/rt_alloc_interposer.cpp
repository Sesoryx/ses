// RT allocation interposer (DESIGN §13) — loaded into the allocation-gate test
// only, via LD_PRELOAD. It counts malloc-family calls made while the engine's
// RtAllocationScope has armed the current thread, then forwards to libc.
//
// Interposing at the libc level (not operator new) is deliberate: the RT path
// calls into dynamically linked soxr, whose internal allocations must also be
// visible to the guard. `ses_rt_alloc_count` is atomic because the RT thread
// increments it while the test thread reads and resets it.
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <cerrno>

// glibc's own entry points: used only for the brief window before the
// preload constructor has resolved the next library in the chain.
extern "C" {
void* __libc_malloc(size_t);
void* __libc_calloc(size_t, size_t);
void* __libc_realloc(void*, size_t);
void* __libc_memalign(size_t, size_t);
}

namespace {

using MallocFn = void* (*)(size_t);
using CallocFn = void* (*)(size_t, size_t);
using ReallocFn = void* (*)(void*, size_t);
using AlignedFn = void* (*)(size_t, size_t);
using PosixMemalignFn = int (*)(void**, size_t, size_t);
using MemalignFn = void* (*)(size_t, size_t);
using StrdupFn = char* (*)(const char*);
using StrndupFn = char* (*)(const char*, size_t);

MallocFn realMalloc = nullptr;
CallocFn realCalloc = nullptr;
ReallocFn realRealloc = nullptr;
AlignedFn realAlignedAlloc = nullptr;
PosixMemalignFn realPosixMemalign = nullptr;
MemalignFn realMemalign = nullptr;
StrdupFn realStrdup = nullptr;
StrndupFn realStrndup = nullptr;

std::atomic<uint64_t> allocationCount{0};
thread_local bool armed = false;

void countAllocation()
{
    if (armed)
        allocationCount.fetch_add(1, std::memory_order_relaxed);
}

// Resolve before any scope can arm: dlsym may allocate internally, and that
// must not be attributed to the RT callback.
__attribute__((constructor)) void resolveRealFunctions()
{
    realMalloc = reinterpret_cast<MallocFn>(dlsym(RTLD_NEXT, "malloc"));
    realCalloc = reinterpret_cast<CallocFn>(dlsym(RTLD_NEXT, "calloc"));
    realRealloc = reinterpret_cast<ReallocFn>(dlsym(RTLD_NEXT, "realloc"));
    realAlignedAlloc = reinterpret_cast<AlignedFn>(dlsym(RTLD_NEXT, "aligned_alloc"));
    realPosixMemalign =
        reinterpret_cast<PosixMemalignFn>(dlsym(RTLD_NEXT, "posix_memalign"));
    realMemalign = reinterpret_cast<MemalignFn>(dlsym(RTLD_NEXT, "memalign"));
    realStrdup = reinterpret_cast<StrdupFn>(dlsym(RTLD_NEXT, "strdup"));
    realStrndup = reinterpret_cast<StrndupFn>(dlsym(RTLD_NEXT, "strndup"));
}

} // namespace

// ---- engine hooks (weak in the engine, strong here) -------------------------
extern "C" void ses_rt_alloc_enter(void)
{
    armed = true;
}

extern "C" void ses_rt_alloc_leave(void)
{
    armed = false;
}

extern "C" uint64_t ses_rt_alloc_count(void)
{
    return allocationCount.load(std::memory_order_relaxed);
}

extern "C" void ses_rt_alloc_reset(void)
{
    allocationCount.store(0, std::memory_order_relaxed);
}

// ---- libc interposition ------------------------------------------------------
extern "C" void* malloc(size_t size)
{
    countAllocation();
    return realMalloc ? realMalloc(size) : __libc_malloc(size);
}

extern "C" void* calloc(size_t count, size_t size)
{
    countAllocation();
    return realCalloc ? realCalloc(count, size) : __libc_calloc(count, size);
}

extern "C" void* realloc(void* ptr, size_t size)
{
    countAllocation();
    return realRealloc ? realRealloc(ptr, size) : __libc_realloc(ptr, size);
}

extern "C" void* aligned_alloc(size_t alignment, size_t size)
{
    countAllocation();
    return realAlignedAlloc ? realAlignedAlloc(alignment, size)
                            : __libc_memalign(alignment, size);
}

extern "C" int posix_memalign(void** ptr, size_t alignment, size_t size)
{
    countAllocation();
    if (realPosixMemalign)
        return realPosixMemalign(ptr, alignment, size);
    void* memory = __libc_memalign(alignment, size);
    if (!memory)
        return ENOMEM;
    *ptr = memory;
    return 0;
}

extern "C" void* memalign(size_t alignment, size_t size)
{
    countAllocation();
    return realMemalign ? realMemalign(alignment, size)
                        : __libc_memalign(alignment, size);
}

extern "C" char* strdup(const char* text)
{
    countAllocation();
    if (realStrdup)
        return realStrdup(text);
    const size_t size = strlen(text) + 1;
    char* copy = static_cast<char*>(realMalloc ? realMalloc(size) : __libc_malloc(size));
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

extern "C" char* strndup(const char* text, size_t maxLength)
{
    countAllocation();
    if (realStrndup)
        return realStrndup(text, maxLength);
    size_t length = 0;
    while (length < maxLength && text[length])
        ++length;
    char* copy = static_cast<char*>(realMalloc ? realMalloc(length + 1)
                                                    : __libc_malloc(length + 1));
    if (copy) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}
