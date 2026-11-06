#include <cyber/core/error.h>
#include <cyber/core/log.h>
#include <cyber/memory/allocator.h>
#include <cyber/platform/thread.h>
#include <cyber/renderer/render_thread.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <utility>

namespace cyber::renderer
{
using detail::RenderThreadImpl;

namespace
{

using cyber::core::ErrorCode;
using cyber::memory::Byte;
using cyber::rhi::kMaxFramesInFlight;

constexpr u32 kCommandCapacity = 8;

[[nodiscard]] Error MakeError(ErrorCode code, const char* message) noexcept
{
    return Error{code, message};
}

void LogCallbackFailure(const char* operation, const Error& error) noexcept
{
    CG_LOG(Error, Renderer, "%s: %s", operation,
           error.message != nullptr ? error.message : "unspecified callback failure");
}

class ProfileScope
{
public:
    ProfileScope(cyber::perf::Profiler& profiler, const char* name) noexcept : m_profiler(profiler)
    {
        m_profiler.BeginScope(name);
    }

    ~ProfileScope()
    {
        m_profiler.EndScope();
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    cyber::perf::Profiler& m_profiler;
};

enum class CommandKind : u32
{
    Frame,
    Resize,
};

struct Command
{
    CommandKind kind = CommandKind::Frame;
    u32 slot = 0;
    u32 width = 0;
    u32 height = 0;
    u64 frameIndex = 0;
};

class SpscCommandQueue
{
public:
    [[nodiscard]] bool Push(const Command& command) noexcept
    {
        const u64 write = m_write.load(std::memory_order_relaxed);
        const u64 read = m_read.load(std::memory_order_acquire);
        if (write - read >= kCommandCapacity)
        {
            return false;
        }

        m_commands[write % kCommandCapacity] = command;
        m_write.store(write + 1U, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool Pop(Command& command) noexcept
    {
        const u64 read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire))
        {
            return false;
        }

        command = m_commands[read % kCommandCapacity];
        m_read.store(read + 1U, std::memory_order_release);
        return true;
    }

private:
    std::array<Command, kCommandCapacity> m_commands{};
    std::atomic<u64> m_write{0};
    std::atomic<u64> m_read{0};
};

struct PacketSlot
{
    std::atomic<bool> inUse{false};
    Byte* data = nullptr;
    usize size = 0;
};

}  // namespace

struct detail::RenderThreadImpl
{
    explicit RenderThreadImpl(const RenderThreadDesc& desc) noexcept
        : allocator(desc.allocator),
          profiler(desc.profiler != nullptr ? desc.profiler : &cyber::perf::NullProfilerInstance()),
          frameCapacity(desc.maxFrameDataBytes), framesInFlight(desc.framesInFlight),
          singleThreaded(desc.singleThreaded), initialize(desc.initialize),
          renderFrame(desc.renderFrame), resize(desc.resize), shutdown(desc.shutdown),
          userData(desc.userData), producerThreadId(cyber::platform::Thread::CurrentThreadId())
    {
    }

    [[nodiscard]] Status<Error> Initialize() noexcept
    {
        if (initialize == nullptr)
        {
            return Status<Error>::Ok();
        }
        ProfileScope scope(*profiler, "Renderer.RenderThread.Initialize");
        EnterCallback();
        const Status<Error> result = initialize(userData);
        LeaveCallback();
        return result;
    }

    [[nodiscard]] Status<Error> ShutdownCallback() noexcept
    {
        if (shutdown == nullptr)
        {
            return Status<Error>::Ok();
        }
        ProfileScope scope(*profiler, "Renderer.RenderThread.Shutdown");
        EnterCallback();
        const Status<Error> result = shutdown(userData);
        LeaveCallback();
        return result;
    }

    void RecordError(const Error& error) noexcept
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        if (!hasError)
        {
            firstError = error;
            hasError = true;
        }
    }

    [[nodiscard]] Status<Error> TakeError() noexcept
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        if (!hasError)
        {
            return Status<Error>::Ok();
        }
        const Error error = firstError;
        hasError = false;
        firstError = {};
        return Status<Error>::Err(error);
    }

    [[nodiscard]] bool IsProducerThread() const noexcept
    {
        return cyber::platform::Thread::CurrentThreadId() == producerThreadId;
    }

    [[nodiscard]] bool IsRenderOwner() const noexcept
    {
        return renderThreadReady.load(std::memory_order_acquire) &&
               cyber::platform::Thread::CurrentThreadId() ==
                   renderThreadId.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool IsInCallbackThread() const noexcept
    {
        return callbackActive.load(std::memory_order_acquire) &&
               cyber::platform::Thread::CurrentThreadId() ==
                   callbackThreadId.load(std::memory_order_relaxed);
    }

    void EnterCallback() noexcept
    {
        callbackThreadId.store(cyber::platform::Thread::CurrentThreadId(),
                               std::memory_order_relaxed);
        callbackActive.store(true, std::memory_order_release);
    }

    void LeaveCallback() noexcept
    {
        callbackActive.store(false, std::memory_order_release);
        callbackThreadId.store(0, std::memory_order_relaxed);
    }

    void WakeConsumer() noexcept
    {
        m_wakeSequence.fetch_add(1U, std::memory_order_release);
        m_wakeSequence.notify_one();
    }

    [[nodiscard]] bool Enqueue(const Command& command) noexcept
    {
        pendingCommands.fetch_add(1U, std::memory_order_relaxed);
        if (!commands.Push(command))
        {
            pendingCommands.fetch_sub(1U, std::memory_order_relaxed);
            return false;
        }
        WakeConsumer();
        return true;
    }

    void FinishCommand() noexcept
    {
        pendingCommands.fetch_sub(1U, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(idleMutex);
        }
        idleCondition.notify_all();
    }

    void Process(const Command& command) noexcept
    {
        if (command.kind == CommandKind::Frame)
        {
            PacketSlot& slot = packets[command.slot];
            const RenderFrameView frame{
                command.frameIndex,
                command.slot,
                Span<const Byte>{slot.data, slot.size},
            };
            {
                ProfileScope scope(*profiler, "Renderer.RenderThread.Frame");
                EnterCallback();
                const Status<Error> result = renderFrame(frame, userData);
                LeaveCallback();
                if (!result)
                {
                    RecordError(result.Error());
                }
            }
            slot.size = 0;
            slot.inUse.store(false, std::memory_order_release);
        }
        else if (resize != nullptr)
        {
            ProfileScope scope(*profiler, "Renderer.RenderThread.Resize");
            EnterCallback();
            const Status<Error> result = resize(command.width, command.height, userData);
            LeaveCallback();
            if (!result)
            {
                RecordError(result.Error());
            }
        }
        FinishCommand();
    }

    void SignalStartup(bool succeeded, const Error& error) noexcept
    {
        {
            std::lock_guard<std::mutex> lock(startupMutex);
            startupSucceeded = succeeded;
            startupError = error;
            startupReady = true;
        }
        startupCondition.notify_all();
    }

    void Run() noexcept
    {
        renderThreadId.store(cyber::platform::Thread::CurrentThreadId(), std::memory_order_release);
        renderThreadReady.store(true, std::memory_order_release);
        const Status<Error> initialized = Initialize();
        if (!initialized)
        {
            RecordError(initialized.Error());
            SignalStartup(false, initialized.Error());
            const Status<Error> stopped = ShutdownCallback();
            if (!stopped)
            {
                RecordError(stopped.Error());
                LogCallbackFailure("render shutdown after failed initialization", stopped.Error());
            }
            return;
        }
        SignalStartup(true, {});

        for (;;)
        {
            const u64 observedWake = m_wakeSequence.load(std::memory_order_acquire);
            Command command{};
            if (commands.Pop(command))
            {
                Process(command);
                continue;
            }
            if (stopRequested.load(std::memory_order_acquire))
            {
                break;
            }
            m_wakeSequence.wait(observedWake, std::memory_order_acquire);
        }

        const Status<Error> stopped = ShutdownCallback();
        if (!stopped)
        {
            RecordError(stopped.Error());
        }
        shutdownComplete.store(true, std::memory_order_release);
    }

    cyber::memory::Allocator* allocator = nullptr;
    cyber::perf::Profiler* profiler = nullptr;
    usize frameCapacity = 0;
    u32 framesInFlight = 0;
    bool singleThreaded = false;
    RenderThreadInitializeFunction initialize = nullptr;
    RenderFrameFunction renderFrame = nullptr;
    RenderResizeFunction resize = nullptr;
    RenderThreadShutdownFunction shutdown = nullptr;
    void* userData = nullptr;
    u64 producerThreadId = 0;
    std::atomic<u64> renderThreadId{0};
    std::atomic<bool> renderThreadReady{false};
    std::atomic<u64> callbackThreadId{0};
    std::atomic<bool> callbackActive{false};
    std::array<PacketSlot, kMaxFramesInFlight> packets{};
    Byte* packetStorage = nullptr;
    SpscCommandQueue commands;
    std::atomic<u64> m_wakeSequence{0};
    std::atomic<u64> pendingCommands{0};
    std::atomic<bool> accepting{true};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> shutdownComplete{false};
    cyber::platform::Thread thread;
    std::mutex startupMutex;
    std::condition_variable startupCondition;
    bool startupReady = false;
    bool startupSucceeded = false;
    Error startupError{};
    std::mutex idleMutex;
    std::condition_variable idleCondition;
    std::mutex errorMutex;
    bool hasError = false;
    Error firstError{};
    u64 nextFrameIndex = 0;
};

namespace
{

void ThreadEntry(void* userData) noexcept
{
    static_cast<detail::RenderThreadImpl*>(userData)->Run();
}

void DestroyImpl(detail::RenderThreadImpl* impl) noexcept
{
    if (impl == nullptr)
    {
        return;
    }
    cyber::memory::Allocator* allocator = impl->allocator;
    if (impl->packetStorage != nullptr)
    {
        allocator->Deallocate(impl->packetStorage);
        impl->packetStorage = nullptr;
    }
    std::destroy_at(impl);
    allocator->Deallocate(impl);
}

[[nodiscard]] Error ProducerError() noexcept
{
    return MakeError(ErrorCode::InvalidArgument, "render thread call must use its producer thread");
}

}  // namespace

RenderThread::RenderThread(RenderThreadImpl* impl) noexcept : m_impl(impl) {}

RenderThread::~RenderThread()
{
    Destroy();
}

RenderThread::RenderThread(RenderThread&& other) noexcept : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

RenderThread& RenderThread::operator=(RenderThread&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Result<RenderThread, Error> RenderThread::Create(const RenderThreadDesc& desc) noexcept
{
    if (desc.allocator == nullptr || desc.renderFrame == nullptr || desc.threadName == nullptr ||
        desc.threadName[0] == '\0')
    {
        return Result<RenderThread, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "invalid render thread configuration"));
    }
    if (desc.framesInFlight == 0 || desc.framesInFlight > kMaxFramesInFlight)
    {
        return Result<RenderThread, Error>::Err(MakeError(
            ErrorCode::OutOfRange, "render frames in flight is outside the supported range"));
    }
    if (desc.maxFrameDataBytes >
        std::numeric_limits<usize>::max() / static_cast<usize>(desc.framesInFlight))
    {
        return Result<RenderThread, Error>::Err(
            MakeError(ErrorCode::Overflow, "render packet storage size overflows"));
    }

    void* storage = desc.allocator->Allocate(sizeof(RenderThreadImpl), alignof(RenderThreadImpl));
    if (storage == nullptr)
    {
        return Result<RenderThread, Error>::Err(
            MakeError(ErrorCode::Internal, "render thread state allocation failed"));
    }
    auto* impl = std::construct_at(static_cast<RenderThreadImpl*>(storage), desc);

    const usize totalBytes = desc.maxFrameDataBytes * static_cast<usize>(desc.framesInFlight);
    if (totalBytes != 0)
    {
        impl->packetStorage =
            static_cast<Byte*>(desc.allocator->Allocate(totalBytes, alignof(Byte)));
        if (impl->packetStorage == nullptr)
        {
            DestroyImpl(impl);
            return Result<RenderThread, Error>::Err(
                MakeError(ErrorCode::Internal, "render packet storage allocation failed"));
        }
        for (u32 slot = 0; slot < desc.framesInFlight; ++slot)
        {
            impl->packets[slot].data =
                impl->packetStorage + (static_cast<usize>(slot) * desc.maxFrameDataBytes);
        }
    }

    if (desc.singleThreaded)
    {
        impl->renderThreadId.store(impl->producerThreadId, std::memory_order_release);
        impl->renderThreadReady.store(true, std::memory_order_release);
        const Status<Error> initialized = impl->Initialize();
        if (!initialized)
        {
            const Status<Error> stopped = impl->ShutdownCallback();
            if (!stopped)
            {
                impl->RecordError(stopped.Error());
                LogCallbackFailure("render shutdown after failed initialization", stopped.Error());
            }
            const Error error = initialized.Error();
            DestroyImpl(impl);
            return Result<RenderThread, Error>::Err(error);
        }
        return Result<RenderThread, Error>::Ok(RenderThread{impl});
    }

    const Status<Error> started =
        impl->thread.Start(*desc.allocator, desc.threadName, ThreadEntry, impl);
    if (!started)
    {
        const Error error = started.Error();
        DestroyImpl(impl);
        return Result<RenderThread, Error>::Err(error);
    }

    bool startupSucceeded = false;
    Error startupError{};
    {
        std::unique_lock<std::mutex> lock(impl->startupMutex);
        impl->startupCondition.wait(lock, [&] { return impl->startupReady; });
        startupSucceeded = impl->startupSucceeded;
        startupError = impl->startupError;
    }
    if (!startupSucceeded)
    {
        impl->thread.Join();
        DestroyImpl(impl);
        return Result<RenderThread, Error>::Err(startupError);
    }

    return Result<RenderThread, Error>::Ok(RenderThread{impl});
}

Result<u64, Error> RenderThread::SubmitFrame(Span<const Byte> frameData) noexcept
{
    if (m_impl == nullptr)
    {
        return Result<u64, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "render thread is empty"));
    }
    if (!m_impl->IsProducerThread())
    {
        return Result<u64, Error>::Err(ProducerError());
    }
    if (!m_impl->accepting.load(std::memory_order_acquire))
    {
        return Result<u64, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "render thread is no longer accepting work"));
    }
    if (m_impl->nextFrameIndex == std::numeric_limits<u64>::max())
    {
        return Result<u64, Error>::Err(
            MakeError(ErrorCode::Overflow, "render frame index is exhausted"));
    }
    if (frameData.Size() != 0 && frameData.Data() == nullptr)
    {
        return Result<u64, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "render frame data is null"));
    }
    if (frameData.Size() > m_impl->frameCapacity)
    {
        return Result<u64, Error>::Err(
            MakeError(ErrorCode::OutOfRange, "render frame data exceeds configured capacity"));
    }

    ProfileScope publishScope(*m_impl->profiler, "Renderer.Frame.Publish");
    u32 slotIndex = m_impl->framesInFlight;
    for (u32 index = 0; index < m_impl->framesInFlight; ++index)
    {
        bool expected = false;
        if (m_impl->packets[index].inUse.compare_exchange_strong(
                expected, true, std::memory_order_acquire, std::memory_order_relaxed))
        {
            slotIndex = index;
            break;
        }
    }
    if (slotIndex == m_impl->framesInFlight)
    {
        return Result<u64, Error>::Err(
            MakeError(ErrorCode::Busy, "all render frame packet slots are in use"));
    }

    PacketSlot& packet = m_impl->packets[slotIndex];
    if (!frameData.Empty())
    {
        std::memcpy(packet.data, frameData.Data(), frameData.Size());
    }
    packet.size = frameData.Size();

    const u64 frameIndex = m_impl->nextFrameIndex;
    const Command command{CommandKind::Frame, slotIndex, 0, 0, frameIndex};
    if (m_impl->singleThreaded)
    {
        const RenderFrameView frame{
            frameIndex,
            slotIndex,
            Span<const Byte>{packet.data, packet.size},
        };
        {
            ProfileScope renderScope(*m_impl->profiler, "Renderer.RenderThread.Frame");
            m_impl->EnterCallback();
            const Status<Error> result = m_impl->renderFrame(frame, m_impl->userData);
            m_impl->LeaveCallback();
            if (!result)
            {
                m_impl->RecordError(result.Error());
            }
        }
        packet.size = 0;
        packet.inUse.store(false, std::memory_order_release);
        ++m_impl->nextFrameIndex;
        return Result<u64, Error>::Ok(frameIndex);
    }

    if (!m_impl->Enqueue(command))
    {
        packet.size = 0;
        packet.inUse.store(false, std::memory_order_release);
        return Result<u64, Error>::Err(MakeError(ErrorCode::Busy, "render command queue is full"));
    }
    ++m_impl->nextFrameIndex;
    return Result<u64, Error>::Ok(frameIndex);
}

Status<Error> RenderThread::RequestResize(u32 width, u32 height) noexcept
{
    if (m_impl == nullptr)
    {
        return Status<Error>::Err(MakeError(ErrorCode::InvalidArgument, "render thread is empty"));
    }
    if (!m_impl->IsProducerThread())
    {
        return Status<Error>::Err(ProducerError());
    }
    if (!m_impl->accepting.load(std::memory_order_acquire))
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "render thread is no longer accepting work"));
    }

    if (m_impl->singleThreaded)
    {
        if (m_impl->resize != nullptr)
        {
            ProfileScope scope(*m_impl->profiler, "Renderer.RenderThread.Resize");
            m_impl->EnterCallback();
            const Status<Error> result = m_impl->resize(width, height, m_impl->userData);
            m_impl->LeaveCallback();
            if (!result)
            {
                m_impl->RecordError(result.Error());
            }
        }
        return Status<Error>::Ok();
    }

    if (!m_impl->Enqueue(Command{CommandKind::Resize, 0, width, height, 0}))
    {
        return Status<Error>::Err(MakeError(ErrorCode::Busy, "render command queue is full"));
    }
    return Status<Error>::Ok();
}

Status<Error> RenderThread::WaitIdle() noexcept
{
    if (m_impl == nullptr || !m_impl->IsProducerThread() || m_impl->IsInCallbackThread())
    {
        return Status<Error>::Err(ProducerError());
    }
    if (!m_impl->singleThreaded)
    {
        std::unique_lock<std::mutex> lock(m_impl->idleMutex);
        m_impl->idleCondition.wait(
            lock, [&] { return m_impl->pendingCommands.load(std::memory_order_acquire) == 0; });
    }
    return m_impl->TakeError();
}

Status<Error> RenderThread::Shutdown() noexcept
{
    if (m_impl == nullptr)
    {
        return Status<Error>::Ok();
    }
    if (!m_impl->IsProducerThread() || m_impl->IsInCallbackThread())
    {
        return Status<Error>::Err(ProducerError());
    }

    if (m_impl->shutdownComplete.load(std::memory_order_acquire))
    {
        return m_impl->TakeError();
    }
    m_impl->accepting.store(false, std::memory_order_release);

    if (m_impl->singleThreaded)
    {
        const Status<Error> stopped = m_impl->ShutdownCallback();
        if (!stopped)
        {
            m_impl->RecordError(stopped.Error());
        }
        m_impl->shutdownComplete.store(true, std::memory_order_release);
        return m_impl->TakeError();
    }

    m_impl->stopRequested.store(true, std::memory_order_release);
    m_impl->WakeConsumer();
    m_impl->thread.Join();
    return m_impl->TakeError();
}

bool RenderThread::IsRenderThread() const noexcept
{
    return m_impl != nullptr && m_impl->IsRenderOwner();
}

u64 RenderThread::RenderThreadId() const noexcept
{
    return m_impl != nullptr ? m_impl->renderThreadId.load(std::memory_order_acquire) : 0;
}

u32 RenderThread::FramesInFlight() const noexcept
{
    return m_impl != nullptr ? m_impl->framesInFlight : 0;
}

bool RenderThread::IsRunning() const noexcept
{
    return m_impl != nullptr && m_impl->accepting.load(std::memory_order_acquire);
}

void RenderThread::Destroy() noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }
    if (!m_impl->IsProducerThread() || m_impl->IsInCallbackThread())
    {
        CG_LOG(Error, Renderer, "%s",
               "render thread must be destroyed by its producer outside callbacks");
        return;
    }
    const Status<Error> result = Shutdown();
    if (!result)
    {
        CG_LOG(Error, Renderer, "%s",
               result.Error().message != nullptr ? result.Error().message
                                                 : "render thread shutdown failed");
    }
    if (m_impl->thread.Joinable())
    {
        m_impl->thread.Join();
    }
    DestroyImpl(m_impl);
    m_impl = nullptr;
}

}  // namespace cyber::renderer
