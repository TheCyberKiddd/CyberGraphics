#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/thread.h>
#include <cyber/test/test.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

using namespace cyber::platform;
using namespace cyber::memory;

namespace
{

struct CounterContext
{
    Atomic<u32>* counter = nullptr;
    u32 iterations = 0;
};

void IncrementMany(void* userData) noexcept
{
    auto* context = static_cast<CounterContext*>(userData);
    for (u32 index = 0; index < context->iterations; ++index)
    {
        context->counter->fetch_add(1, std::memory_order_relaxed);
    }
}

struct WaitContext
{
    Mutex* mutex = nullptr;
    ConditionVariable* condition = nullptr;
    Atomic<bool>* ready = nullptr;
    Atomic<bool>* wake = nullptr;
};

void WaitForSignal(void* userData) noexcept
{
    auto* context = static_cast<WaitContext*>(userData);
    MutexGuard guard(*context->mutex);
    context->ready->store(true, std::memory_order_release);
    while (!context->wake->load(std::memory_order_acquire))
    {
        context->condition->Wait(*context->mutex);
    }
}

void DoNothing(void*) noexcept {}

}  // namespace

CG_TEST(platform_thread_starts_and_joins)
{
    HeapAllocator heap;
    Atomic<u32> counter{0};
    CounterContext context{&counter, 2000};

    Thread first;
    Thread second;
    CG_EXPECT_TRUE(static_cast<bool>(first.Start(heap, "counter-a", &IncrementMany, &context)));
    CG_EXPECT_TRUE(static_cast<bool>(second.Start(heap, "counter-b", &IncrementMany, &context)));
    CG_EXPECT_TRUE(first.Joinable());
    CG_EXPECT_TRUE(second.Joinable());
    CG_EXPECT_TRUE(first.ThreadId() != 0);
    CG_EXPECT_TRUE(second.ThreadId() != 0);

    first.Join();
    second.Join();
    CG_EXPECT_FALSE(first.Joinable());
    CG_EXPECT_EQ(counter.load(), u32{4000});
}

CG_TEST(platform_thread_move_and_errors)
{
    HeapAllocator heap;
    Thread thread;
    CG_EXPECT_FALSE(thread.Joinable());
    thread.Join();
    CG_EXPECT_FALSE(static_cast<bool>(thread.Start(heap, "bad", nullptr, nullptr)));

    CG_EXPECT_TRUE(static_cast<bool>(thread.Start(heap, "nothing", &DoNothing, nullptr)));
    Thread moved = std::move(thread);
    CG_EXPECT_FALSE(thread.Joinable());
    CG_EXPECT_TRUE(moved.Joinable());

    Thread assigned;
    assigned = std::move(moved);
    CG_EXPECT_FALSE(moved.Joinable());
    CG_EXPECT_TRUE(assigned.Joinable());
    assigned.Join();
}

CG_TEST(platform_mutex_and_condition_variable)
{
    HeapAllocator heap;
    Mutex mutex;
    ConditionVariable condition;
    Atomic<bool> ready{false};
    Atomic<bool> wake{false};
    WaitContext context{&mutex, &condition, &ready, &wake};

    Thread thread;
    CG_REQUIRE(static_cast<bool>(thread.Start(heap, "waiter", &WaitForSignal, &context)));

    while (!ready.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    {
        MutexGuard guard(mutex);
        wake.store(true, std::memory_order_release);
        condition.NotifyOne();
    }

    thread.Join();
    CG_EXPECT_TRUE(wake.load());

    CG_EXPECT_TRUE(mutex.TryLock());
    mutex.Unlock();
    {
        MutexGuard guard(mutex);
        CG_EXPECT_FALSE(mutex.TryLock());
    }
    CG_EXPECT_TRUE(mutex.TryLock());
    mutex.Unlock();
}

CG_TEST(platform_thread_ids)
{
    CG_EXPECT_TRUE(Thread::CurrentThreadId() != 0);
    Thread::SetCurrentThreadName("main-test");
}
