#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/allocator.h>
#include <cyber/platform/types.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace cyber::platform
{

using ThreadFunction = void (*)(void* userData) noexcept;

// Native thread with explicit start, join, and naming. A joinable Thread joins
// in its destructor; engine code never detaches.
class Thread
{
public:
    Thread() noexcept = default;
    ~Thread();

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(Thread&& other) noexcept;
    Thread& operator=(Thread&& other) noexcept;

    // The start context and the copied thread name are allocated from allocator.
    // On failure no thread is created and the previous state is preserved.
    [[nodiscard]] core::Status<core::Error> Start(memory::Allocator& allocator, const char* name,
                                                  ThreadFunction function, void* userData) noexcept;

    void Join() noexcept;

    [[nodiscard]] bool Joinable() const noexcept;
    [[nodiscard]] u64 NativeHandle() const noexcept;
    [[nodiscard]] u64 ThreadId() const noexcept;

    static void SetCurrentThreadName(const char* name) noexcept;
    [[nodiscard]] static u64 CurrentThreadId() noexcept;

private:
    u64 m_handle = 0;
    u64 m_id = 0;
    bool m_joinable = false;
    void* m_context = nullptr;
    memory::Allocator* m_allocator = nullptr;
};

// Thin standard-library wrappers. They are not recursive and are not movable.
class Mutex
{
public:
    Mutex() = default;
    ~Mutex() = default;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock() noexcept;
    [[nodiscard]] bool TryLock() noexcept;
    void Unlock() noexcept;

    // BasicLockable spelling for std::unique_lock and condition_variable_any.
    void lock() noexcept
    {
        Lock();
    }

    [[nodiscard]] bool try_lock() noexcept
    {
        return TryLock();
    }

    void unlock() noexcept
    {
        Unlock();
    }

private:
    std::mutex m_mutex;
};

class ConditionVariable
{
public:
    ConditionVariable() = default;
    ~ConditionVariable() = default;

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    void Wait(Mutex& mutex) noexcept;
    void NotifyOne() noexcept;
    void NotifyAll() noexcept;

private:
    std::condition_variable_any m_condition;
};

class MutexGuard
{
public:
    explicit MutexGuard(Mutex& mutex) noexcept : m_mutex(mutex)
    {
        m_mutex.Lock();
    }

    ~MutexGuard()
    {
        m_mutex.Unlock();
    }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

private:
    Mutex& m_mutex;
};

template <typename T>
using Atomic = std::atomic<T>;

}  // namespace cyber::platform
