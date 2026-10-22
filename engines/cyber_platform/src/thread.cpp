#include <cyber/platform/thread.h>

#include <utility>

namespace cyber::platform
{

Thread::~Thread()
{
    Join();
}

Thread::Thread(Thread&& other) noexcept
    : m_handle(other.m_handle), m_id(other.m_id), m_joinable(other.m_joinable),
      m_context(other.m_context), m_allocator(other.m_allocator)
{
    other.m_handle = 0;
    other.m_id = 0;
    other.m_joinable = false;
    other.m_context = nullptr;
    other.m_allocator = nullptr;
}

Thread& Thread::operator=(Thread&& other) noexcept
{
    if (this != &other)
    {
        Join();
        m_handle = other.m_handle;
        m_id = other.m_id;
        m_joinable = other.m_joinable;
        m_context = other.m_context;
        m_allocator = other.m_allocator;
        other.m_handle = 0;
        other.m_id = 0;
        other.m_joinable = false;
        other.m_context = nullptr;
        other.m_allocator = nullptr;
    }
    return *this;
}

bool Thread::Joinable() const noexcept
{
    return m_joinable;
}

u64 Thread::NativeHandle() const noexcept
{
    return m_handle;
}

u64 Thread::ThreadId() const noexcept
{
    return m_id;
}

void Mutex::Lock() noexcept
{
    m_mutex.lock();
}

bool Mutex::TryLock() noexcept
{
    return m_mutex.try_lock();
}

void Mutex::Unlock() noexcept
{
    m_mutex.unlock();
}

void ConditionVariable::Wait(Mutex& mutex) noexcept
{
    // The caller holds the mutex through a MutexGuard. Adopt it for the wait so
    // the condition variable can unlock and relock it atomically, then release
    // ownership back to the caller's guard.
    std::unique_lock<Mutex> lock(mutex, std::adopt_lock);
    m_condition.wait(lock);
    lock.release();
}

void ConditionVariable::NotifyOne() noexcept
{
    m_condition.notify_one();
}

void ConditionVariable::NotifyAll() noexcept
{
    m_condition.notify_all();
}

}  // namespace cyber::platform
