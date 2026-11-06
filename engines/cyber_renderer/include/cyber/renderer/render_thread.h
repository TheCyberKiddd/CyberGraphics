#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/perf/profiler.h>
#include <cyber/rhi/frame_ring.h>

namespace cyber::renderer
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Span;
using cyber::memory::usize;

inline constexpr u32 kDefaultRenderFramesInFlight = 2;

namespace detail
{

struct RenderThreadImpl;

}  // namespace detail

/// Immutable view of one copied frame packet.
struct RenderFrameView
{
    /// Monotonic index assigned when the producer accepts the frame.
    u64 frameIndex = 0;
    /// CPU packet slot, reusable after this callback returns.
    u32 slot = 0;
    /// Frame bytes copied before publication; valid only for this callback.
    Span<const cyber::memory::Byte> data;
};

/// Non-throwing callback invoked to create render-owned state.
using RenderThreadInitializeFunction = Status<Error> (*)(void* userData) noexcept;
/// Non-throwing callback that consumes one immutable frame packet.
using RenderFrameFunction = Status<Error> (*)(const RenderFrameView& frame,
                                              void* userData) noexcept;
/// Non-throwing callback invoked in queue order for a surface resize.
using RenderResizeFunction = Status<Error> (*)(u32 width, u32 height, void* userData) noexcept;
/// Non-throwing callback invoked after accepted work drains.
using RenderThreadShutdownFunction = Status<Error> (*)(void* userData) noexcept;

/// Configuration and callback ownership for a render-thread pipeline.
struct RenderThreadDesc
{
    /// Allocator for startup state, packet storage, and the native thread context;
    /// it must outlive the pipeline and must not be shared unsynchronized with callbacks.
    cyber::memory::Allocator* allocator = nullptr;
    /// Thread-safe profiler that outlives the RenderThread; null uses the no-op profiler.
    cyber::perf::Profiler* profiler = nullptr;
    /// Maximum CPU packets outstanding at once. Defaults to two.
    u32 framesInFlight = kDefaultRenderFramesInFlight;
    /// Maximum bytes copied for each packet. Zero permits only empty packets.
    usize maxFrameDataBytes = 64U * 1024U;
    /// Run the same callbacks inline on the producer thread for debugging.
    bool singleThreaded = false;
    /// Optional render-owner initialization callback.
    RenderThreadInitializeFunction initialize = nullptr;
    /// Required render-owner callback for each accepted frame.
    RenderFrameFunction renderFrame = nullptr;
    /// Optional ordered resize callback; zero dimensions represent minimization.
    RenderResizeFunction resize = nullptr;
    /// Optional render-owner teardown callback, after queued work drains.
    RenderThreadShutdownFunction shutdown = nullptr;
    /// Borrowed callback context; must outlive the RenderThread.
    void* userData = nullptr;
    /// Thread name copied by the platform thread implementation.
    const char* threadName = "Cyber Render";
};

/// Bounded producer-to-render-owner frame handoff with an optional worker thread.
///
/// The creating thread is the sole producer and lifecycle owner. Frame data is
/// copied into allocator-backed slots before publication. RHI objects and
/// callback state are owned by the callbacks, which execute on the render
/// thread, or inline on the producer in single-threaded mode.
class RenderThread
{
public:
    /// Constructs an empty, non-running pipeline.
    RenderThread() noexcept = default;

    /// Stops and joins a running pipeline on its producer thread.
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    /// Transfers pipeline ownership; the source becomes empty.
    /// @param other Pipeline to take ownership of.
    RenderThread(RenderThread&& other) noexcept;

    /// Stops this pipeline if needed, then transfers ownership from `other`.
    /// @param other Pipeline to take ownership of.
    /// @return This pipeline.
    RenderThread& operator=(RenderThread&& other) noexcept;

    /// Creates the pipeline and invokes initialization before returning.
    /// @param desc Allocator, callbacks, queue limits, and optional profiler.
    /// @return A started pipeline, or InvalidArgument, OutOfRange, Overflow, or
    /// Internal if configuration, storage allocation, thread creation, or
    /// initialization fails.
    [[nodiscard]] static Result<RenderThread, Error> Create(const RenderThreadDesc& desc) noexcept;

    /// Copies and publishes one frame without waiting for execution in threaded
    /// mode; single-threaded mode executes the callbacks inline.
    /// @param frameData Immutable bytes to copy; an empty span is valid.
    /// @return The monotonic frame index, or Busy when bounded storage is full,
    /// OutOfRange when the payload exceeds its configured capacity, or
    /// InvalidArgument when called from a non-producer thread or after shutdown.
    [[nodiscard]] Result<u64, Error>
    SubmitFrame(Span<const cyber::memory::Byte> frameData) noexcept;

    /// Enqueues an ordered resize; zero dimensions are valid for minimization.
    /// @param width New surface width in pixels.
    /// @param height New surface height in pixels.
    /// @return Success, Busy when the command ring is full, or InvalidArgument
    /// when called from a non-producer thread or after shutdown.
    [[nodiscard]] Status<Error> RequestResize(u32 width, u32 height) noexcept;

    /// Waits for every accepted command and returns the first callback failure.
    /// @return Success or the first asynchronous callback error. InvalidArgument
    /// when called from a non-producer thread or a render callback.
    [[nodiscard]] Status<Error> WaitIdle() noexcept;

    /// Stops admission, drains accepted commands, runs teardown, and joins.
    /// @return Success or the first callback failure; InvalidArgument if called
    /// from a non-producer thread or a render callback.
    [[nodiscard]] Status<Error> Shutdown() noexcept;

    /// Reports whether the current thread is the designated render owner.
    /// @return True on the render thread, or on the producer in single-threaded mode.
    [[nodiscard]] bool IsRenderThread() const noexcept;

    /// Returns the native identity of the render owner, or zero before startup.
    /// @return Render-thread identity; in single-threaded mode this is the producer.
    [[nodiscard]] u64 RenderThreadId() const noexcept;

    /// Returns the configured maximum number of CPU packets in flight.
    /// @return Frame-slot count, or zero for an empty/moved-from object.
    [[nodiscard]] u32 FramesInFlight() const noexcept;

    /// Reports whether new frame and resize commands are currently accepted.
    /// @return True while the pipeline is accepting work.
    [[nodiscard]] bool IsRunning() const noexcept;

private:
    explicit RenderThread(detail::RenderThreadImpl* impl) noexcept;
    void Destroy() noexcept;

    detail::RenderThreadImpl* m_impl = nullptr;
};

}  // namespace cyber::renderer
