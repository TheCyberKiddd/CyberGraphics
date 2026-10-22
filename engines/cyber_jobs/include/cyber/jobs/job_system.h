#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/handle.h>
#include <cyber/perf/profiler.h>

namespace cyber::jobs
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;

struct JobTag
{
};

using JobHandle = memory::Handle<JobTag>;

// Non-throwing job entry points. User data is non-owning and must outlive the
// job.
using JobFunction = void (*)(void* userData) noexcept;
using ParallelFunction = void (*)(void* userData, u32 index) noexcept;

struct Impl;

struct JobDesc
{
    const char* name = "job";
    JobFunction function = nullptr;
    void* userData = nullptr;
};

// Fixed-worker scheduler with work stealing, dependency counters, and
// generation-checked handles. All scheduling storage is preallocated from the
// allocator passed to Create.
class JobSystem
{
public:
    struct Desc
    {
        // 0 selects the hardware thread count.
        u32 workerCount = 0;
        // Non-owning. Null selects the null profiler.
        perf::Profiler* profiler = nullptr;
    };

    JobSystem() noexcept = default;
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    JobSystem(JobSystem&& other) noexcept;
    JobSystem& operator=(JobSystem&& other) noexcept;

    [[nodiscard]] static core::Result<JobSystem, core::Error>
    Create(const Desc& desc, memory::Allocator& allocator) noexcept;

    // Submits and returns a handle that Wait consumes. Returns an invalid handle
    // when the job is invalid, the system is stopping, or storage is exhausted.
    [[nodiscard]] JobHandle Submit(const JobDesc& desc) noexcept;

    // Runs after dependency completes. If the dependency is already complete,
    // the job is submitted immediately. An invalid dependency returns an invalid
    // handle.
    [[nodiscard]] JobHandle SubmitAfter(JobHandle dependency, const JobDesc& desc) noexcept;

    // Fire-and-forget submission. Returns false when the job is invalid, the
    // system is stopping, or no worker queue has room.
    [[nodiscard]] bool SubmitDetached(const JobDesc& desc) noexcept;

    // Waits for completion, helps execute ready work before blocking, and
    // consumes the handle. An invalid or stale handle returns immediately.
    void Wait(JobHandle handle) noexcept;

    // Waits until all active jobs complete. Does not help execute work.
    void WaitIdle() noexcept;

    // Calls function(userData, index) for every index in [0, count). Batches are
    // detached and the call waits for all batches. count == 0 is a no-op.
    // Nested ParallelFor from a worker is not supported in the first pass.
    void ParallelFor(u32 count, u32 batchSize, ParallelFunction function, void* userData) noexcept;

    // Stops admission, drains queued work, and joins every worker. Idempotent.
    void Shutdown() noexcept;

    [[nodiscard]] u32 WorkerCount() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] bool IsWorkerThread() const noexcept;

    [[nodiscard]] static u32 HardwareThreadCount() noexcept;

private:
    explicit JobSystem(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

}  // namespace cyber::jobs
