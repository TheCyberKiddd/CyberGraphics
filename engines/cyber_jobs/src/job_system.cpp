#include <cyber/jobs/job_system.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/platform/thread.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>

namespace cyber::jobs
{

namespace
{

constexpr u32 kJobCapacity = 16384;
constexpr u32 kQueueCapacity = kJobCapacity;

struct Job
{
    JobFunction function = nullptr;
    void* userData = nullptr;
    const char* name = "job";
    std::atomic<u32> pendingDependencies{0};
    std::atomic<bool> complete{false};
    std::atomic<bool> finished{false};
    Job* dependents = nullptr;
    Job* nextDependent = nullptr;
    u32 slotIndex = memory::kInvalidHandleIndex;
    u32 slotGeneration = 0;
};

// Job records own no resources, so they can be recycled without destruction
// bookkeeping at shutdown.
static_assert(std::is_trivially_destructible_v<Job>);

struct Slot
{
    Job* job = nullptr;
    u32 generation = 1;
    u32 nextFree = memory::kInvalidHandleIndex;
};

class JobQueue
{
public:
    void Init(Job** storage, u32 capacity) noexcept
    {
        m_storage = storage;
        m_capacity = capacity;
        m_head = 0;
        m_count = 0;
    }

    [[nodiscard]] bool Push(Job* job) noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_storage == nullptr || m_count == m_capacity)
        {
            return false;
        }
        m_storage[(m_head + m_count) % m_capacity] = job;
        ++m_count;
        return true;
    }

    [[nodiscard]] Job* PopBack() noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count == 0)
        {
            return nullptr;
        }
        const u32 index = (m_head + m_count - 1U) % m_capacity;
        Job* job = m_storage[index];
        --m_count;
        return job;
    }

    [[nodiscard]] Job* PopFront() noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count == 0)
        {
            return nullptr;
        }
        Job* job = m_storage[m_head];
        m_head = (m_head + 1U) % m_capacity;
        --m_count;
        return job;
    }

private:
    Job** m_storage = nullptr;
    u32 m_capacity = 0;
    u32 m_head = 0;
    u32 m_count = 0;
    std::mutex m_mutex;
};

}  // namespace

struct Impl;

namespace
{

struct Worker
{
    Impl* system = nullptr;
    u32 index = 0;
    JobQueue queue;
    Job** queueStorage = nullptr;
    platform::Thread thread;
    std::atomic<u64> threadId{0};
};

struct ParallelState
{
    Impl* system = nullptr;
    ParallelFunction function = nullptr;
    void* userData = nullptr;
    u32 count = 0;
    u32 batchSize = 1;
    std::atomic<u32> next{0};
    std::atomic<u32> remaining{0};
};

}  // namespace

struct Impl
{
    Impl(memory::Allocator& allocator, u32 count, perf::Profiler* profiler) noexcept
        : allocator(&allocator),
          profiler(profiler != nullptr ? profiler : &perf::NullProfilerInstance()),
          workerCount(count), slots(allocator)
    {
    }

    ~Impl();

    [[nodiscard]] core::Status<core::Error> Initialize() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] Job* AllocateJob() noexcept
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        if (freeJobCount == 0)
        {
            return nullptr;
        }
        const u32 index = freeJobIndices[--freeJobCount];
        return std::construct_at(jobStorage + index);
    }

    void FreeJob(Job* job) noexcept
    {
        if (job == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(jobMutex);
        const auto index = static_cast<u32>(job - jobStorage);
        if (index >= kJobCapacity || freeJobCount >= kJobCapacity)
        {
            return;
        }
        std::destroy_at(job);
        freeJobIndices[freeJobCount++] = index;
    }

    [[nodiscard]] bool AllocateSlot(Job* job, JobHandle& handle) noexcept
    {
        std::lock_guard<std::mutex> lock(slotMutex);
        if (freeSlot == memory::kInvalidHandleIndex)
        {
            return false;
        }

        const u32 index = freeSlot;
        Slot& slot = slots[index];
        freeSlot = slot.nextFree;
        slot.nextFree = memory::kInvalidHandleIndex;
        slot.job = job;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }

        job->slotIndex = index;
        job->slotGeneration = slot.generation;
        handle.index = index;
        handle.generation = slot.generation;
        return true;
    }

    [[nodiscard]] Job* GetJob(JobHandle handle) noexcept
    {
        std::lock_guard<std::mutex> lock(slotMutex);
        if (handle.index >= slots.Size() || slots[handle.index].job == nullptr ||
            slots[handle.index].generation != handle.generation)
        {
            return nullptr;
        }
        return slots[handle.index].job;
    }

    [[nodiscard]] Job* DetachSlot(JobHandle handle) noexcept
    {
        std::lock_guard<std::mutex> lock(slotMutex);
        if (handle.index >= slots.Size())
        {
            return nullptr;
        }

        Slot& slot = slots[handle.index];
        if (slot.job == nullptr || slot.generation != handle.generation)
        {
            return nullptr;
        }

        Job* job = slot.job;
        slot.job = nullptr;
        ++slot.generation;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }
        slot.nextFree = freeSlot;
        freeSlot = handle.index;
        if (job != nullptr)
        {
            job->slotIndex = memory::kInvalidHandleIndex;
        }
        return job;
    }

    [[nodiscard]] bool PrepareJob(const JobDesc& desc, bool withHandle, JobHandle& handle,
                                  Job*& job) noexcept
    {
        if (!accepting.load(std::memory_order_acquire) || desc.function == nullptr)
        {
            return false;
        }

        job = AllocateJob();
        if (job == nullptr)
        {
            return false;
        }

        job->function = desc.function;
        job->userData = desc.userData;
        job->name = desc.name != nullptr ? desc.name : "job";
        job->slotIndex = memory::kInvalidHandleIndex;
        job->slotGeneration = 0;

        if (withHandle && !AllocateSlot(job, handle))
        {
            FreeJob(job);
            job = nullptr;
            return false;
        }

        activeJobs.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    void CancelJob(JobHandle handle, Job* job) noexcept
    {
        if (handle.IsValid())
        {
            (void)DetachSlot(handle);
        }
        activeJobs.fetch_sub(1, std::memory_order_acq_rel);
        FreeJob(job);
        NotifyWaiters();
    }

    [[nodiscard]] bool Enqueue(Job* job) noexcept
    {
        if (!accepting.load(std::memory_order_acquire) || workerCount == 0)
        {
            return false;
        }

        pendingJobs.fetch_add(1, std::memory_order_acq_rel);
        const u32 start = nextWorker.fetch_add(1, std::memory_order_relaxed) % workerCount;
        for (u32 offset = 0; offset < workerCount; ++offset)
        {
            Worker& worker = workers[(start + offset) % workerCount];
            if (worker.queue.Push(job))
            {
                NotifyWorkers();
                return true;
            }
        }
        pendingJobs.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }

    void EnqueueReady(Job* job) noexcept
    {
        if (!Enqueue(job))
        {
            // Queue capacity is sized for every live job, so this is a safety
            // net rather than the normal path. Run inline to avoid losing work.
            ExecuteJob(job);
        }
    }

    [[nodiscard]] Job* PopJob(u32 index) noexcept
    {
        Job* job = workers[index].queue.PopBack();
        if (job != nullptr)
        {
            pendingJobs.fetch_sub(1, std::memory_order_acq_rel);
            return job;
        }

        for (u32 offset = 1; offset < workerCount; ++offset)
        {
            Worker& worker = workers[(index + offset) % workerCount];
            job = worker.queue.PopFront();
            if (job != nullptr)
            {
                pendingJobs.fetch_sub(1, std::memory_order_acq_rel);
                return job;
            }
        }
        return nullptr;
    }

    [[nodiscard]] Job* PopAny() noexcept
    {
        for (u32 index = 0; index < workerCount; ++index)
        {
            Job* job = workers[index].queue.PopBack();
            if (job != nullptr)
            {
                pendingJobs.fetch_sub(1, std::memory_order_acq_rel);
                return job;
            }
        }
        for (u32 index = 0; index < workerCount; ++index)
        {
            Job* job = workers[index].queue.PopFront();
            if (job != nullptr)
            {
                pendingJobs.fetch_sub(1, std::memory_order_acq_rel);
                return job;
            }
        }
        return nullptr;
    }

    void ExecuteJob(Job* job) noexcept
    {
        profiler->BeginScope(job->name);
        job->function(job->userData);
        profiler->EndScope();
        CompleteJob(job);
    }

    void CompleteJob(Job* job) noexcept
    {
        Job* dependents = nullptr;
        {
            std::lock_guard<std::mutex> lock(dependencyMutex);
            job->complete.store(true, std::memory_order_release);
            dependents = job->dependents;
            job->dependents = nullptr;
        }

        while (dependents != nullptr)
        {
            Job* next = dependents->nextDependent;
            dependents->nextDependent = nullptr;
            if (dependents->pendingDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                EnqueueReady(dependents);
            }
            dependents = next;
        }

        const bool detached = job->slotIndex == memory::kInvalidHandleIndex;
        job->finished.store(true, std::memory_order_release);
        activeJobs.fetch_sub(1, std::memory_order_acq_rel);
        NotifyWaiters();
        NotifyWorkers();

        if (detached)
        {
            FreeJob(job);
        }
    }

    void WorkerMain(Worker* worker) noexcept
    {
        worker->threadId.store(platform::Thread::CurrentThreadId(), std::memory_order_release);
        while (true)
        {
            Job* job = PopJob(worker->index);
            if (job != nullptr)
            {
                ExecuteJob(job);
                continue;
            }

            workWaiters.fetch_add(1, std::memory_order_acq_rel);
            {
                std::unique_lock<std::mutex> lock(workMutex);
                workCondition.wait(lock,
                                   [this]()
                                   {
                                       return (stopping.load(std::memory_order_acquire) &&
                                               activeJobs.load(std::memory_order_acquire) == 0) ||
                                              pendingJobs.load(std::memory_order_acquire) > 0;
                                   });
            }
            workWaiters.fetch_sub(1, std::memory_order_acq_rel);
            if (stopping.load(std::memory_order_acquire) &&
                activeJobs.load(std::memory_order_acquire) == 0)
            {
                return;
            }
        }
    }

    static void WorkerEntry(void* userData) noexcept;

    void NotifyWaiters() noexcept
    {
        // The waiter count is only a hint; a waiter increments it before its
        // first predicate check, so a skipped notification is always observed
        // by that check. Holding the mutex while notifying closes the gap
        // between a predicate check and blocking.
        if (waiters.load(std::memory_order_acquire) == 0)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(waitMutex);
        waitCondition.notify_all();
    }

    void NotifyWorkers() noexcept
    {
        if (workWaiters.load(std::memory_order_acquire) == 0)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(workMutex);
        workCondition.notify_all();
    }

    void StopAndJoin() noexcept
    {
        accepting.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(workMutex);
            stopping.store(true, std::memory_order_release);
            workCondition.notify_all();
        }

        for (u32 index = 0; index < constructedWorkers; ++index)
        {
            workers[index].thread.Join();
        }
    }

    void ReleaseWorkers() noexcept
    {
        if (workers == nullptr)
        {
            return;
        }
        for (u32 index = 0; index < constructedWorkers; ++index)
        {
            workers[index].thread.Join();
            if (workers[index].queueStorage != nullptr)
            {
                allocator->Deallocate(workers[index].queueStorage);
                workers[index].queueStorage = nullptr;
            }
            workers[index].~Worker();
        }
        allocator->Deallocate(workers);
        workers = nullptr;
        constructedWorkers = 0;
    }

    memory::Allocator* allocator = nullptr;
    perf::Profiler* profiler = nullptr;
    u32 workerCount = 0;
    Worker* workers = nullptr;
    u32 constructedWorkers = 0;
    Job* jobStorage = nullptr;
    u32* freeJobIndices = nullptr;
    u32 freeJobCount = 0;
    memory::DynamicArray<Slot> slots;
    u32 freeSlot = memory::kInvalidHandleIndex;
    std::mutex slotMutex;
    std::mutex jobMutex;
    std::mutex dependencyMutex;
    std::mutex waitMutex;
    std::condition_variable waitCondition;
    std::atomic<u32> waiters{0};
    std::mutex workMutex;
    std::condition_variable workCondition;
    std::atomic<u32> workWaiters{0};
    std::atomic<u32> activeJobs{0};
    std::atomic<u32> pendingJobs{0};
    std::atomic<u32> nextWorker{0};
    std::atomic<bool> accepting{true};
    std::atomic<bool> stopping{false};
    std::atomic<bool> shutdown{false};
};

Impl::~Impl()
{
    if (!shutdown.load(std::memory_order_acquire))
    {
        Shutdown();
    }
    ReleaseWorkers();
    if (freeJobIndices != nullptr)
    {
        allocator->Deallocate(freeJobIndices);
        freeJobIndices = nullptr;
    }
    if (jobStorage != nullptr)
    {
        allocator->Deallocate(jobStorage);
        jobStorage = nullptr;
    }
}

core::Status<core::Error> Impl::Initialize() noexcept
{
    if (workerCount == 0)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "job storage is unavailable"});
    }

    const usize jobBytes = sizeof(Job) * static_cast<usize>(kJobCapacity);
    jobStorage = static_cast<Job*>(allocator->Allocate(jobBytes, alignof(Job)));
    if (jobStorage == nullptr)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate job records"});
    }

    const usize freeIndexBytes = sizeof(u32) * static_cast<usize>(kJobCapacity);
    freeJobIndices = static_cast<u32*>(allocator->Allocate(freeIndexBytes, alignof(u32)));
    if (freeJobIndices == nullptr)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate the job free list"});
    }
    for (u32 index = 0; index < kJobCapacity; ++index)
    {
        freeJobIndices[index] = index;
    }
    freeJobCount = kJobCapacity;

    if (!slots.TryResize(kJobCapacity))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate job slots"});
    }
    for (u32 index = 0; index < kJobCapacity; ++index)
    {
        Slot& slot = slots[index];
        slot.job = nullptr;
        slot.generation = 1;
        slot.nextFree = index + 1U < kJobCapacity ? index + 1U : memory::kInvalidHandleIndex;
    }
    freeSlot = 0;

    const usize workerBytes = sizeof(Worker) * static_cast<usize>(workerCount);
    workers = static_cast<Worker*>(allocator->Allocate(workerBytes, alignof(Worker)));
    if (workers == nullptr)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate workers"});
    }

    for (u32 index = 0; index < workerCount; ++index)
    {
        std::construct_at(workers + index);
        workers[index].system = this;
        workers[index].index = index;
        ++constructedWorkers;
    }

    for (u32 index = 0; index < workerCount; ++index)
    {
        const usize bytes = static_cast<usize>(kQueueCapacity) * sizeof(Job*);
        workers[index].queueStorage = static_cast<Job**>(allocator->Allocate(bytes, alignof(Job*)));
        if (workers[index].queueStorage == nullptr)
        {
            Shutdown();
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Internal, "failed to allocate a worker queue"});
        }
        workers[index].queue.Init(workers[index].queueStorage, kQueueCapacity);
    }

    for (u32 index = 0; index < workerCount; ++index)
    {
        core::Status<core::Error> status = workers[index].thread.Start(
            *allocator, "cyber_worker", &Impl::WorkerEntry, &workers[index]);
        if (!status)
        {
            Shutdown();
            return status;
        }
    }

    return core::Status<core::Error>::Ok();
}

void Impl::Shutdown() noexcept
{
    if (shutdown.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }
    StopAndJoin();
}

void Impl::WorkerEntry(void* userData) noexcept
{
    auto* worker = static_cast<Worker*>(userData);
    worker->system->WorkerMain(worker);
}

namespace
{

void ParallelBatch(void* userData) noexcept
{
    auto* state = static_cast<ParallelState*>(userData);
    while (true)
    {
        const u32 start = state->next.fetch_add(state->batchSize, std::memory_order_relaxed);
        if (start >= state->count)
        {
            break;
        }
        u32 end = start + state->batchSize;
        if (end > state->count)
        {
            end = state->count;
        }
        for (u32 index = start; index < end; ++index)
        {
            state->function(state->userData, index);
        }
    }

    state->remaining.fetch_sub(1, std::memory_order_acq_rel);
    state->system->NotifyWaiters();
}

}  // namespace

JobSystem::JobSystem(Impl* impl) noexcept : m_impl(impl) {}

JobSystem::~JobSystem()
{
    if (m_impl != nullptr)
    {
        memory::Allocator* allocator = m_impl->allocator;
        m_impl->Shutdown();
        m_impl->~Impl();
        allocator->Deallocate(m_impl);
        m_impl = nullptr;
    }
}

JobSystem::JobSystem(JobSystem&& other) noexcept : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

JobSystem& JobSystem::operator=(JobSystem&& other) noexcept
{
    if (this != &other)
    {
        if (m_impl != nullptr)
        {
            memory::Allocator* allocator = m_impl->allocator;
            m_impl->Shutdown();
            m_impl->~Impl();
            allocator->Deallocate(m_impl);
        }
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

core::Result<JobSystem, core::Error> JobSystem::Create(const Desc& desc,
                                                       memory::Allocator& allocator) noexcept
{
    u32 count = desc.workerCount == 0 ? HardwareThreadCount() : desc.workerCount;
    if (count == 0)
    {
        count = 1;
    }

    void* memory = allocator.Allocate(sizeof(Impl), alignof(Impl));
    if (memory == nullptr)
    {
        return core::Result<JobSystem, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate the job system"});
    }

    auto* impl = static_cast<Impl*>(memory);
    std::construct_at(impl, allocator, count, desc.profiler);
    core::Status<core::Error> status = impl->Initialize();
    if (!status)
    {
        impl->~Impl();
        allocator.Deallocate(memory);
        return core::Result<JobSystem, core::Error>::Err(status.Error());
    }
    return core::Result<JobSystem, core::Error>::Ok(JobSystem(impl));
}

JobHandle JobSystem::Submit(const JobDesc& desc) noexcept
{
    if (m_impl == nullptr)
    {
        return JobHandle{};
    }

    JobHandle handle{};
    Job* job = nullptr;
    if (!m_impl->PrepareJob(desc, true, handle, job))
    {
        return JobHandle{};
    }
    if (!m_impl->Enqueue(job))
    {
        m_impl->CancelJob(handle, job);
        return JobHandle{};
    }
    return handle;
}

JobHandle JobSystem::SubmitAfter(JobHandle dependency, const JobDesc& desc) noexcept
{
    if (m_impl == nullptr)
    {
        return JobHandle{};
    }

    Job* dependencyJob = m_impl->GetJob(dependency);
    if (dependencyJob == nullptr)
    {
        return JobHandle{};
    }

    JobHandle handle{};
    Job* job = nullptr;
    if (!m_impl->PrepareJob(desc, true, handle, job))
    {
        return JobHandle{};
    }

    bool immediate = false;
    {
        std::lock_guard<std::mutex> lock(m_impl->dependencyMutex);
        if (dependencyJob->complete.load(std::memory_order_acquire))
        {
            immediate = true;
        }
        else
        {
            job->pendingDependencies.store(1, std::memory_order_release);
            job->nextDependent = dependencyJob->dependents;
            dependencyJob->dependents = job;
        }
    }

    if (immediate && !m_impl->Enqueue(job))
    {
        m_impl->CancelJob(handle, job);
        return JobHandle{};
    }
    return handle;
}

bool JobSystem::SubmitDetached(const JobDesc& desc) noexcept
{
    if (m_impl == nullptr)
    {
        return false;
    }

    JobHandle ignored{};
    Job* job = nullptr;
    if (!m_impl->PrepareJob(desc, false, ignored, job))
    {
        return false;
    }
    if (!m_impl->Enqueue(job))
    {
        m_impl->CancelJob(ignored, job);
        return false;
    }
    return true;
}

void JobSystem::Wait(JobHandle handle) noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }

    Job* job = m_impl->GetJob(handle);
    if (job == nullptr)
    {
        return;
    }

    m_impl->waiters.fetch_add(1, std::memory_order_acq_rel);
    while (!job->finished.load(std::memory_order_acquire))
    {
        Job* ready = m_impl->PopAny();
        if (ready != nullptr)
        {
            m_impl->ExecuteJob(ready);
            continue;
        }

        std::unique_lock<std::mutex> lock(m_impl->waitMutex);
        m_impl->waitCondition.wait(lock, [job]()
                                   { return job->finished.load(std::memory_order_acquire); });
    }
    m_impl->waiters.fetch_sub(1, std::memory_order_acq_rel);

    Job* detached = m_impl->DetachSlot(handle);
    if (detached != nullptr)
    {
        m_impl->FreeJob(detached);
    }
}

void JobSystem::WaitIdle() noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }
    m_impl->waiters.fetch_add(1, std::memory_order_acq_rel);
    std::unique_lock<std::mutex> lock(m_impl->waitMutex);
    m_impl->waitCondition.wait(lock, [this]()
                               { return m_impl->activeJobs.load(std::memory_order_acquire) == 0; });
    m_impl->waiters.fetch_sub(1, std::memory_order_acq_rel);
}

void JobSystem::ParallelFor(u32 count, u32 batchSize, ParallelFunction function,
                            void* userData) noexcept
{
    if (m_impl == nullptr || count == 0 || function == nullptr)
    {
        return;
    }
    if (batchSize == 0)
    {
        batchSize = 1;
    }

    const u32 batchCount = count / batchSize + (count % batchSize != 0 ? 1U : 0U);
    const u32 maxJobs = m_impl->workerCount * 2U;
    const u32 jobCount = batchCount < maxJobs ? batchCount : maxJobs;
    if (jobCount == 0)
    {
        return;
    }

    ParallelState state{};
    state.system = m_impl;
    state.function = function;
    state.userData = userData;
    state.count = count;
    state.batchSize = batchSize;
    state.remaining.store(jobCount, std::memory_order_release);

    JobDesc desc{};
    desc.name = "parallel_for";
    desc.function = &ParallelBatch;
    desc.userData = &state;

    for (u32 index = 0; index < jobCount; ++index)
    {
        if (!SubmitDetached(desc))
        {
            if (state.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                m_impl->NotifyWaiters();
            }
        }
    }

    m_impl->waiters.fetch_add(1, std::memory_order_acq_rel);
    std::unique_lock<std::mutex> lock(m_impl->waitMutex);
    m_impl->waitCondition.wait(lock, [&state]()
                               { return state.remaining.load(std::memory_order_acquire) == 0; });
    m_impl->waiters.fetch_sub(1, std::memory_order_acq_rel);
}

void JobSystem::Shutdown() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->Shutdown();
    }
}

u32 JobSystem::WorkerCount() const noexcept
{
    return m_impl != nullptr ? m_impl->workerCount : 0;
}

bool JobSystem::IsRunning() const noexcept
{
    return m_impl != nullptr && !m_impl->shutdown.load(std::memory_order_acquire);
}

bool JobSystem::IsWorkerThread() const noexcept
{
    if (m_impl == nullptr)
    {
        return false;
    }
    const u64 id = platform::Thread::CurrentThreadId();
    for (u32 index = 0; index < m_impl->constructedWorkers; ++index)
    {
        if (m_impl->workers[index].threadId.load(std::memory_order_acquire) == id)
        {
            return true;
        }
    }
    return false;
}

u32 JobSystem::HardwareThreadCount() noexcept
{
    const u32 count = std::thread::hardware_concurrency();
    return count == 0 ? 1 : count;
}

}  // namespace cyber::jobs
