#include <cyber/jobs/job_system.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/test/test.h>

#include <atomic>
#include <utility>
#include <vector>

using namespace cyber::jobs;
using namespace cyber::memory;

namespace
{

struct Counter
{
    std::atomic<u32> value{0};
};

void Increment(void* userData) noexcept
{
    static_cast<Counter*>(userData)->value.fetch_add(1, std::memory_order_relaxed);
}

struct DependencyContext
{
    std::atomic<u32>* order = nullptr;
    std::atomic<u32>* value = nullptr;
};

void FirstDependency(void* userData) noexcept
{
    auto* context = static_cast<DependencyContext*>(userData);
    context->order->store(1, std::memory_order_release);
    context->value->fetch_add(1, std::memory_order_relaxed);
}

void SecondDependency(void* userData) noexcept
{
    auto* context = static_cast<DependencyContext*>(userData);
    if (context->order->load(std::memory_order_acquire) == 1)
    {
        context->value->fetch_add(1, std::memory_order_relaxed);
    }
}

struct ParallelContext
{
    std::atomic<u64> sum{0};
    std::atomic<u32> calls{0};
};

void AddIndex(void* userData, u32 index) noexcept
{
    auto* context = static_cast<ParallelContext*>(userData);
    context->sum.fetch_add(static_cast<u64>(index), std::memory_order_relaxed);
    context->calls.fetch_add(1, std::memory_order_relaxed);
}

struct WorkerCheck
{
    JobSystem* system = nullptr;
    std::atomic<bool> isWorker{false};
};

void CheckWorker(void* userData) noexcept
{
    auto* context = static_cast<WorkerCheck*>(userData);
    context->isWorker.store(context->system->IsWorkerThread(), std::memory_order_release);
}

}  // namespace

CG_TEST(jobs_create_submit_and_wait)
{
    HeapAllocator heap;
    JobSystem::Desc desc{};
    desc.workerCount = 2;
    auto created = JobSystem::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    JobSystem system = std::move(created.Value());

    CG_EXPECT_TRUE(system.IsRunning());
    CG_EXPECT_EQ(system.WorkerCount(), u32{2});
    CG_EXPECT_FALSE(system.IsWorkerThread());
    CG_EXPECT_TRUE(JobSystem::HardwareThreadCount() > 0);

    Counter counter;
    JobDesc job{};
    job.name = "increment";
    job.function = &Increment;
    job.userData = &counter;

    JobHandle handle = system.Submit(job);
    CG_EXPECT_TRUE(handle.IsValid());
    system.Wait(handle);
    CG_EXPECT_EQ(counter.value.load(), u32{1});
    system.Wait(handle);  // Stale handles are ignored.

    WorkerCheck worker{&system};
    JobDesc workerJob{};
    workerJob.name = "worker-check";
    workerJob.function = &CheckWorker;
    workerJob.userData = &worker;
    JobHandle workerHandle = system.Submit(workerJob);
    CG_EXPECT_TRUE(workerHandle.IsValid());
    system.WaitIdle();
    system.Wait(workerHandle);
    CG_EXPECT_TRUE(worker.isWorker.load());
}

CG_TEST(jobs_dependencies_and_detached)
{
    HeapAllocator heap;
    JobSystem::Desc desc{};
    desc.workerCount = 2;
    auto created = JobSystem::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    JobSystem system = std::move(created.Value());

    std::atomic<u32> order{0};
    std::atomic<u32> value{0};
    DependencyContext context{&order, &value};

    JobDesc first{};
    first.name = "first";
    first.function = &FirstDependency;
    first.userData = &context;

    JobDesc second{};
    second.name = "second";
    second.function = &SecondDependency;
    second.userData = &context;

    JobHandle firstHandle = system.Submit(first);
    CG_REQUIRE(firstHandle.IsValid());
    JobHandle secondHandle = system.SubmitAfter(firstHandle, second);
    CG_REQUIRE(secondHandle.IsValid());

    system.Wait(secondHandle);
    CG_EXPECT_EQ(value.load(), u32{2});
    system.Wait(firstHandle);

    Counter detached;
    JobDesc detachedJob{};
    detachedJob.name = "detached";
    detachedJob.function = &Increment;
    detachedJob.userData = &detached;
    CG_EXPECT_TRUE(system.SubmitDetached(detachedJob));
    system.WaitIdle();
    CG_EXPECT_EQ(detached.value.load(), u32{1});
    CG_EXPECT_FALSE(system.SubmitDetached(JobDesc{}));
}

CG_TEST(jobs_parallel_for)
{
    HeapAllocator heap;
    JobSystem::Desc desc{};
    desc.workerCount = 3;
    auto created = JobSystem::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    JobSystem system = std::move(created.Value());

    ParallelContext context;
    system.ParallelFor(1000, 7, &AddIndex, &context);
    CG_EXPECT_EQ(context.calls.load(), u32{1000});
    CG_EXPECT_EQ(context.sum.load(), u64{499500});

    system.ParallelFor(0, 1, &AddIndex, &context);
    CG_EXPECT_EQ(context.calls.load(), u32{1000});
}

CG_TEST(jobs_many_handles_and_generations)
{
    HeapAllocator heap;
    JobSystem::Desc desc{};
    desc.workerCount = 2;
    auto created = JobSystem::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    JobSystem system = std::move(created.Value());

    Counter counter;
    JobDesc job{};
    job.name = "bulk";
    job.function = &Increment;
    job.userData = &counter;

    std::vector<JobHandle> handles;
    handles.reserve(2000);
    for (u32 index = 0; index < 2000; ++index)
    {
        handles.push_back(system.Submit(job));
    }
    for (JobHandle handle : handles)
    {
        CG_EXPECT_TRUE(handle.IsValid());
        system.Wait(handle);
    }
    CG_EXPECT_EQ(counter.value.load(), u32{2000});

    JobHandle invalid = system.Submit(JobDesc{});
    CG_EXPECT_FALSE(invalid.IsValid());
    system.Wait(invalid);
    CG_EXPECT_FALSE(system.SubmitAfter(invalid, job).IsValid());

    system.Shutdown();
    CG_EXPECT_FALSE(system.IsRunning());
    system.Shutdown();
    CG_EXPECT_FALSE(system.Submit(job).IsValid());
}

CG_TEST(jobs_move_and_destroy)
{
    HeapAllocator heap;
    JobSystem::Desc desc{};
    desc.workerCount = 1;
    auto created = JobSystem::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));

    JobSystem first = std::move(created.Value());
    JobSystem second = std::move(first);
    CG_EXPECT_FALSE(first.IsRunning());
    CG_EXPECT_TRUE(second.IsRunning());
    CG_EXPECT_EQ(second.WorkerCount(), u32{1});
    second.Shutdown();
}

namespace
{

constexpr u32 kScalingJobCount = 10000;
constexpr u32 kScalingIterations = 2048;

// Each job does a fixed amount of unpredictable work so the measurement reflects
// parallel throughput rather than per-submission overhead alone. The seed is
// supplied at run time, so the loop cannot be folded away.
struct ScalingContext
{
    std::atomic<u64> sum{0};
    std::atomic<u32> count{0};
    std::atomic<u64> nextSeed{0x9E3779B97F4A7C15ULL};
};

void ScalingWork(void* userData) noexcept
{
    auto* context = static_cast<ScalingContext*>(userData);
    u64 value = context->nextSeed.fetch_add(0x9E3779B97F4A7C15ULL, std::memory_order_relaxed);
    for (u32 iteration = 0; iteration < kScalingIterations; ++iteration)
    {
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
    }
    context->sum.fetch_add(value, std::memory_order_relaxed);
    context->count.fetch_add(1, std::memory_order_relaxed);
}

// Runs kScalingJobCount detached jobs on the requested worker count and proves
// every job ran. workerCount == 0 selects the hardware thread count.
void RunDetachedScaling(u32 workerCount)
{
    HeapAllocator heap;
    JobSystem::Desc desc{};
    desc.workerCount = workerCount;
    auto created = JobSystem::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    JobSystem system = std::move(created.Value());

    ScalingContext context;
    JobDesc job{};
    job.name = "scaling";
    job.function = &ScalingWork;
    job.userData = &context;

    u32 submitted = 0;
    for (u32 index = 0; index < kScalingJobCount; ++index)
    {
        if (system.SubmitDetached(job))
        {
            ++submitted;
        }
    }
    CG_EXPECT_EQ(submitted, kScalingJobCount);
    system.WaitIdle();
    CG_EXPECT_EQ(context.count.load(), kScalingJobCount);
}

class CountingProfiler final : public cyber::perf::Profiler
{
public:
    void BeginScope(const char*) noexcept override
    {
        begins.fetch_add(1, std::memory_order_relaxed);
    }

    void EndScope() noexcept override
    {
        ends.fetch_add(1, std::memory_order_relaxed);
    }

    void Counter(const char*, cyber::core::f64) noexcept override {}

    std::atomic<u32> begins{0};
    std::atomic<u32> ends{0};
};

}  // namespace

CG_TEST(jobs_scaling_one_worker)
{
    RunDetachedScaling(1);
}

CG_TEST(jobs_scaling_two_workers)
{
    RunDetachedScaling(2);
}

CG_TEST(jobs_scaling_four_workers)
{
    RunDetachedScaling(4);
}

CG_TEST(jobs_scaling_hardware_workers)
{
    RunDetachedScaling(0);
}

CG_TEST(jobs_oversubscription_completes)
{
    const u32 oversubscribed = JobSystem::HardwareThreadCount() + 8;
    RunDetachedScaling(oversubscribed);
}

CG_TEST(jobs_repeated_create_and_shutdown)
{
    HeapAllocator heap;
    for (u32 cycle = 0; cycle < 5; ++cycle)
    {
        JobSystem::Desc desc{};
        desc.workerCount = cycle + 1;
        auto created = JobSystem::Create(desc, heap);
        CG_REQUIRE(static_cast<bool>(created));
        JobSystem system = std::move(created.Value());

        Counter counter;
        JobDesc job{};
        job.name = "cycle";
        job.function = &Increment;
        job.userData = &counter;
        CG_EXPECT_TRUE(system.SubmitDetached(job));
        system.WaitIdle();
        CG_EXPECT_EQ(counter.value.load(), u32{1});

        system.Shutdown();
        CG_EXPECT_FALSE(system.IsRunning());
        CG_EXPECT_FALSE(system.SubmitDetached(job));
    }
}

// Regression: a completion notified while WaitIdle is between its predicate
// check and blocking must not be lost. A single worker and a wait after every
// submission make the old race reliable.
CG_TEST(jobs_wait_idle_does_not_miss_completion)
{
    HeapAllocator heap;
    JobSystem::Desc desc{};
    desc.workerCount = 1;
    auto created = JobSystem::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    JobSystem system = std::move(created.Value());

    Counter counter;
    JobDesc job{};
    job.name = "wake";
    job.function = &Increment;
    job.userData = &counter;

    for (u32 cycle = 0; cycle < 200; ++cycle)
    {
        CG_EXPECT_TRUE(system.SubmitDetached(job));
        system.WaitIdle();
    }
    CG_EXPECT_EQ(counter.value.load(), u32{200});
}

CG_TEST(jobs_profiler_scopes_balance)
{
    HeapAllocator heap;
    CountingProfiler profiler;
    JobSystem::Desc desc{};
    desc.workerCount = 3;
    desc.profiler = &profiler;
    auto created = JobSystem::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    JobSystem system = std::move(created.Value());

    Counter counter;
    JobDesc job{};
    job.name = "profiled";
    job.function = &Increment;
    job.userData = &counter;

    for (u32 index = 0; index < 100; ++index)
    {
        CG_EXPECT_TRUE(system.SubmitDetached(job));
    }

    ParallelContext parallel;
    system.ParallelFor(500, 5, &AddIndex, &parallel);
    system.WaitIdle();

    CG_EXPECT_EQ(counter.value.load(), u32{100});
    CG_EXPECT_EQ(parallel.calls.load(), u32{500});
    CG_EXPECT_TRUE(profiler.begins.load() > 0);
    CG_EXPECT_EQ(profiler.begins.load(), profiler.ends.load());
}
