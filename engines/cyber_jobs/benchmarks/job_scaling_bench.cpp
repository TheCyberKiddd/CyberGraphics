// Phase 5 job-scaling evidence.
//
// Runs the same 10,000 detached jobs with a fixed amount of work per job at
// 1, 2, 4, and hardware worker counts, then prints wall time and speedup. This
// is a developer tool; it is not installed and never linked into runtime code.

#include <cyber/core/error.h>
#include <cyber/jobs/job_system.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/time.h>

#include <cstdio>
#include <utility>

namespace
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::jobs::JobDesc;
using cyber::jobs::JobSystem;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::platform::Clock;

constexpr u32 kJobCount = 10000;
constexpr u32 kIterationsPerJob = 50000;
constexpr u32 kRepeats = 3;

struct JobContext
{
    u64 seed = 0;
    u64 result = 0;
    u32 iterations = 0;
};

void Work(void* userData) noexcept
{
    auto* context = static_cast<JobContext*>(userData);
    u64 value = context->seed + 0x9e3779b97f4a7c15ULL;
    for (u32 iteration = 0; iteration < context->iterations; ++iteration)
    {
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
    }
    context->result = value;
}

struct RunResult
{
    double milliseconds = 0.0;
    u32 workers = 0;
    bool valid = false;
};

[[nodiscard]] RunResult RunOnce(HeapAllocator& heap, u32 workerCount) noexcept
{
    RunResult run;

    JobSystem::Desc desc{};
    desc.workerCount = workerCount;
    auto created = JobSystem::Create(desc, heap);
    if (!created)
    {
        std::printf("job system creation failed: %s\n",
                    cyber::core::ErrorCodeName(created.Error().code));
        return run;
    }
    JobSystem system = std::move(created.Value());
    run.workers = system.WorkerCount();

    DynamicArray<JobContext> contexts(heap);
    if (!contexts.TryResize(kJobCount))
    {
        std::printf("job context allocation failed\n");
        return run;
    }

    JobDesc job{};
    job.name = "bench";
    job.function = &Work;

    Clock clock;
    const u64 start = clock.Ticks();
    bool submitted = true;
    for (u32 index = 0; index < kJobCount; ++index)
    {
        JobContext& context = contexts.Data()[index];
        context.seed = index;
        context.result = 0;
        context.iterations = kIterationsPerJob;
        job.userData = &context;
        if (!system.SubmitDetached(job))
        {
            submitted = false;
            break;
        }
    }
    system.WaitIdle();
    const u64 end = clock.Ticks();

    bool completed = true;
    for (u32 index = 0; index < kJobCount; ++index)
    {
        if (contexts.Data()[index].result == 0)
        {
            completed = false;
            break;
        }
    }

    system.Shutdown();

    run.milliseconds = static_cast<double>(end - start) / 1.0e6;
    run.valid = submitted && completed;
    return run;
}

[[nodiscard]] RunResult Measure(HeapAllocator& heap, u32 workerCount) noexcept
{
    RunResult best;
    for (u32 repeat = 0; repeat < kRepeats; ++repeat)
    {
        const RunResult run = RunOnce(heap, workerCount);
        if (!run.valid)
        {
            return run;
        }
        if (!best.valid || run.milliseconds < best.milliseconds)
        {
            best = run;
        }
    }
    return best;
}

}  // namespace

int main()
{
    HeapAllocator heap;

    const u32 hardware = JobSystem::HardwareThreadCount();
    const u32 configurations[] = {1, 2, 4, hardware};

    std::printf("jobs=%u iterations_per_job=%u hardware_threads=%u\n", kJobCount, kIterationsPerJob,
                hardware);

    // Warm up worker startup and the allocator before timing.
    (void)RunOnce(heap, 1);

    double baseline = 0.0;
    bool first = true;
    for (const u32 workers : configurations)
    {
        const RunResult run = Measure(heap, workers);
        if (!run.valid)
        {
            std::printf("workers=%u failed\n", workers);
            return 1;
        }
        if (first)
        {
            baseline = run.milliseconds;
            first = false;
        }
        const double speedup = baseline > 0.0 ? baseline / run.milliseconds : 0.0;
        const double jobsPerSecond =
            run.milliseconds > 0.0 ? static_cast<double>(kJobCount) / (run.milliseconds * 1.0e-3)
                                   : 0.0;
        std::printf("workers=%u wall_ms=%.2f speedup=%.2fx jobs_per_second=%.0f\n", run.workers,
                    run.milliseconds, speedup, jobsPerSecond);
    }

    return 0;
}
