#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/thread.h>
#include <cyber/renderer/render_thread.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/test/test.h>

#include <array>
#include <atomic>
#include <cstring>
#include <thread>
#include <utility>

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::renderer::RenderFrameView;
using cyber::renderer::RenderThread;
using cyber::renderer::RenderThreadDesc;
using cyber::rhi::Device;
using cyber::rhi::DeviceDesc;

namespace
{

constexpr u32 kInitializeEvent = 1;
constexpr u32 kShutdownEvent = 999;

struct TestContext
{
    HeapAllocator allocator;
    Device device;
    RenderThread* renderThread = nullptr;
    std::thread::id producerId = std::this_thread::get_id();
    std::thread::id initializeId{};
    std::thread::id frameId{};
    bool singleThreaded = false;
    bool initializeOnExpectedThread = false;
    bool callbacksOnRenderOwner = true;
    std::array<u32, 32> events{};
    u32 eventCount = 0;
    std::array<u32, 8> frameValues{};
    u32 frameCount = 0;
    std::atomic<bool> frameStarted{false};
    std::atomic<bool> releaseFrame{false};
    bool blockFrame = false;
};

[[nodiscard]] Status<Error> ErrorStatus(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

void AddEvent(TestContext& context, u32 event) noexcept
{
    if (context.eventCount < context.events.size())
    {
        context.events[context.eventCount++] = event;
    }
}

[[nodiscard]] bool CallbackOnRenderOwner(TestContext& context) noexcept
{
    if (context.renderThread == nullptr)
    {
        return true;
    }
    return context.renderThread->IsRenderThread();
}

[[nodiscard]] Status<Error> InitializeDevice(void* userData) noexcept
{
    auto& context = *static_cast<TestContext*>(userData);
    context.initializeId = std::this_thread::get_id();
    context.initializeOnExpectedThread = context.singleThreaded
                                             ? context.initializeId == context.producerId
                                             : context.initializeId != context.producerId;
    DeviceDesc desc{};
    desc.backend = cyber::rhi::NullBackend();
    auto device = Device::Create(desc, context.allocator);
    if (!device)
    {
        return Status<Error>::Err(device.Error());
    }
    context.device = std::move(device.Value());
    AddEvent(context, kInitializeEvent);
    return Status<Error>::Ok();
}

[[nodiscard]] Status<Error> RenderFrame(const RenderFrameView& frame, void* userData) noexcept
{
    auto& context = *static_cast<TestContext*>(userData);
    context.frameId = std::this_thread::get_id();
    context.callbacksOnRenderOwner =
        context.callbacksOnRenderOwner && CallbackOnRenderOwner(context);
    if (!context.device.IsValid())
    {
        return ErrorStatus(ErrorCode::Internal, "RHI device was not initialized on render owner");
    }
    if (frame.data.Size() != 0)
    {
        const u32 value = std::to_integer<u32>(frame.data[0]);
        if (context.frameCount < context.frameValues.size())
        {
            context.frameValues[context.frameCount++] = value;
        }
        AddEvent(context, 100U + value);
    }
    return Status<Error>::Ok();
}

[[nodiscard]] Status<Error> BlockingFrame(const RenderFrameView& frame, void* userData) noexcept
{
    auto& context = *static_cast<TestContext*>(userData);
    context.frameStarted.store(true, std::memory_order_release);
    context.frameStarted.notify_all();
    if (context.blockFrame)
    {
        context.releaseFrame.wait(false, std::memory_order_acquire);
    }
    return RenderFrame(frame, userData);
}

[[nodiscard]] Status<Error> Resize(u32 width, u32 height, void* userData) noexcept
{
    auto& context = *static_cast<TestContext*>(userData);
    context.callbacksOnRenderOwner =
        context.callbacksOnRenderOwner && CallbackOnRenderOwner(context);
    AddEvent(context, 1000U + width + height);
    return Status<Error>::Ok();
}

[[nodiscard]] Status<Error> ShutdownDevice(void* userData) noexcept
{
    auto& context = *static_cast<TestContext*>(userData);
    context.callbacksOnRenderOwner =
        context.callbacksOnRenderOwner && CallbackOnRenderOwner(context);
    if (context.device.IsValid())
    {
        const Status<Error> idle = context.device.WaitIdle();
        if (!idle)
        {
            return idle;
        }
        context.device = Device{};
    }
    AddEvent(context, kShutdownEvent);
    return Status<Error>::Ok();
}

[[nodiscard]] RenderThreadDesc MakeDesc(TestContext& context) noexcept
{
    RenderThreadDesc desc{};
    desc.allocator = &context.allocator;
    desc.maxFrameDataBytes = 16;
    desc.initialize = InitializeDevice;
    desc.renderFrame = RenderFrame;
    desc.resize = Resize;
    desc.shutdown = ShutdownDevice;
    desc.userData = &context;
    return desc;
}

[[nodiscard]] Status<Error> FailFrame(const RenderFrameView&, void*) noexcept
{
    return ErrorStatus(ErrorCode::DeviceLost, "test render callback failure");
}

class OverlapProfiler final : public cyber::perf::Profiler
{
public:
    void BeginScope(const char* name) noexcept override
    {
        if (std::strcmp(name, "Renderer.Frame.Publish") == 0)
        {
            producerId.store(cyber::platform::Thread::CurrentThreadId(), std::memory_order_release);
            publishing.store(true, std::memory_order_release);
        }
        else if (std::strcmp(name, "Renderer.RenderThread.Frame") == 0)
        {
            renderId.store(cyber::platform::Thread::CurrentThreadId(), std::memory_order_release);
            if (publishing.load(std::memory_order_acquire))
            {
                overlapped.store(true, std::memory_order_release);
            }
            frameStarted.store(true, std::memory_order_release);
            frameStarted.notify_all();
        }
    }

    void EndScope() noexcept override
    {
        if (cyber::platform::Thread::CurrentThreadId() ==
                producerId.load(std::memory_order_acquire) &&
            publishing.load(std::memory_order_acquire))
        {
            frameStarted.wait(false, std::memory_order_acquire);
            publishing.store(false, std::memory_order_release);
        }
    }

    void Counter(const char*, cyber::core::f64) noexcept override {}

    std::atomic<u64> producerId{0};
    std::atomic<u64> renderId{0};
    std::atomic<bool> publishing{false};
    std::atomic<bool> frameStarted{false};
    std::atomic<bool> overlapped{false};
};

}  // namespace

CG_TEST(renderer_render_thread_copies_packets_orders_resize_and_shutdown)
{
    TestContext context;
    context.blockFrame = true;
    RenderThreadDesc desc = MakeDesc(context);
    desc.renderFrame = BlockingFrame;
    auto created = RenderThread::Create(desc);
    CG_REQUIRE(created.IsOk());
    RenderThread renderThread = std::move(created.Value());
    context.renderThread = &renderThread;

    Byte first[]{static_cast<Byte>(42)};
    Byte second[]{static_cast<Byte>(7)};
    const auto firstIndex = renderThread.SubmitFrame(Span<const Byte>{first, 1});
    CG_REQUIRE(firstIndex.IsOk());
    CG_EXPECT_EQ(firstIndex.Value(), u64{0});
    context.frameStarted.wait(false, std::memory_order_acquire);
    first[0] = static_cast<Byte>(77);

    const auto secondIndex = renderThread.SubmitFrame(Span<const Byte>{second, 1});
    CG_REQUIRE(secondIndex.IsOk());
    CG_EXPECT_EQ(secondIndex.Value(), u64{1});
    const auto full = renderThread.SubmitFrame(Span<const Byte>{second, 1});
    CG_EXPECT_TRUE(full.IsError());
    if (full.IsError())
    {
        CG_EXPECT_EQ(full.Error().code, ErrorCode::Busy);
    }

    context.releaseFrame.store(true, std::memory_order_release);
    context.releaseFrame.notify_all();
    CG_REQUIRE(renderThread.RequestResize(640, 360).IsOk());
    CG_REQUIRE(renderThread.WaitIdle().IsOk());
    const auto thirdIndex = renderThread.SubmitFrame(Span<const Byte>{second, 1});
    CG_REQUIRE(thirdIndex.IsOk());
    CG_EXPECT_EQ(thirdIndex.Value(), u64{2});
    CG_REQUIRE(renderThread.WaitIdle().IsOk());
    CG_REQUIRE(renderThread.Shutdown().IsOk());

    CG_EXPECT_TRUE(context.initializeOnExpectedThread);
    CG_EXPECT_TRUE(context.callbacksOnRenderOwner);
    CG_EXPECT_EQ(context.frameCount, u32{3});
    CG_EXPECT_EQ(context.frameValues[0], u32{42});
    CG_EXPECT_EQ(context.frameValues[1], u32{7});
    CG_EXPECT_EQ(context.frameValues[2], u32{7});
    CG_EXPECT_EQ(context.eventCount, u32{6});
    CG_EXPECT_EQ(context.events[0], kInitializeEvent);
    CG_EXPECT_EQ(context.events[1], u32{142});
    CG_EXPECT_EQ(context.events[2], u32{107});
    CG_EXPECT_EQ(context.events[3], u32{2000});
    CG_EXPECT_EQ(context.events[4], u32{107});
    CG_EXPECT_EQ(context.events[5], kShutdownEvent);
    CG_EXPECT_NE(context.initializeId, context.producerId);
    CG_EXPECT_NE(context.frameId, context.producerId);
}

CG_TEST(renderer_render_thread_single_thread_mode_uses_same_callbacks)
{
    TestContext context;
    context.singleThreaded = true;
    RenderThreadDesc desc = MakeDesc(context);
    desc.singleThreaded = true;
    auto created = RenderThread::Create(desc);
    CG_REQUIRE(created.IsOk());
    RenderThread renderThread = std::move(created.Value());
    context.renderThread = &renderThread;

    const Byte value[]{static_cast<Byte>(9)};
    const auto frame = renderThread.SubmitFrame(Span<const Byte>{value, 1});
    CG_REQUIRE(frame.IsOk());
    CG_REQUIRE(renderThread.RequestResize(0, 0).IsOk());
    CG_REQUIRE(renderThread.WaitIdle().IsOk());
    CG_REQUIRE(renderThread.Shutdown().IsOk());

    CG_EXPECT_TRUE(context.initializeOnExpectedThread);
    CG_EXPECT_TRUE(context.callbacksOnRenderOwner);
    CG_EXPECT_EQ(context.frameCount, u32{1});
    CG_EXPECT_EQ(context.frameValues[0], u32{9});
    CG_EXPECT_EQ(context.initializeId, context.producerId);
    CG_EXPECT_EQ(context.frameId, context.producerId);
    CG_EXPECT_EQ(context.events[2], u32{1000});
}

CG_TEST(renderer_render_thread_profiler_proves_producer_consumer_overlap)
{
    TestContext context;
    OverlapProfiler profiler;
    RenderThreadDesc desc = MakeDesc(context);
    desc.profiler = &profiler;
    auto created = RenderThread::Create(desc);
    CG_REQUIRE(created.IsOk());
    RenderThread renderThread = std::move(created.Value());
    context.renderThread = &renderThread;

    const Byte value[]{static_cast<Byte>(1)};
    CG_REQUIRE(renderThread.SubmitFrame(Span<const Byte>{value, 1}).IsOk());
    CG_REQUIRE(renderThread.WaitIdle().IsOk());
    CG_REQUIRE(renderThread.Shutdown().IsOk());

    CG_EXPECT_TRUE(profiler.overlapped.load(std::memory_order_acquire));
    CG_EXPECT_NE(profiler.producerId.load(std::memory_order_acquire),
                 profiler.renderId.load(std::memory_order_acquire));
}

CG_TEST(renderer_render_thread_command_ring_backpressure_preserves_frame_sequence)
{
    TestContext context;
    context.blockFrame = true;
    RenderThreadDesc desc = MakeDesc(context);
    desc.renderFrame = BlockingFrame;
    auto created = RenderThread::Create(desc);
    CG_REQUIRE(created.IsOk());
    RenderThread renderThread = std::move(created.Value());
    context.renderThread = &renderThread;

    const Byte first[]{static_cast<Byte>(3)};
    const Byte second[]{static_cast<Byte>(5)};
    const auto firstIndex = renderThread.SubmitFrame(Span<const Byte>{first, 1});
    CG_REQUIRE(firstIndex.IsOk());
    context.frameStarted.wait(false, std::memory_order_acquire);

    u32 acceptedResizes = 0;
    bool queueWasFull = false;
    for (u32 resizeIndex = 0; resizeIndex < 32; ++resizeIndex)
    {
        const Status<Error> resized = renderThread.RequestResize(resizeIndex, 0);
        if (!resized)
        {
            CG_EXPECT_EQ(resized.Error().code, ErrorCode::Busy);
            queueWasFull = true;
            break;
        }
        ++acceptedResizes;
    }
    CG_EXPECT_TRUE(queueWasFull);
    CG_EXPECT_TRUE(acceptedResizes > 0);

    const auto rejectedFrame = renderThread.SubmitFrame(Span<const Byte>{second, 1});
    CG_EXPECT_TRUE(rejectedFrame.IsError());
    if (rejectedFrame.IsError())
    {
        CG_EXPECT_EQ(rejectedFrame.Error().code, ErrorCode::Busy);
    }

    context.releaseFrame.store(true, std::memory_order_release);
    context.releaseFrame.notify_all();
    CG_REQUIRE(renderThread.WaitIdle().IsOk());
    const auto secondIndex = renderThread.SubmitFrame(Span<const Byte>{second, 1});
    CG_REQUIRE(secondIndex.IsOk());
    CG_EXPECT_EQ(secondIndex.Value(), u64{1});
    CG_REQUIRE(renderThread.Shutdown().IsOk());
    CG_EXPECT_EQ(context.frameCount, u32{2});
    CG_EXPECT_EQ(context.frameValues[0], u32{3});
    CG_EXPECT_EQ(context.frameValues[1], u32{5});
    CG_EXPECT_EQ(context.eventCount, acceptedResizes + 4U);
}

CG_TEST(renderer_render_thread_resize_shutdown_stress_drains_in_order)
{
    constexpr u32 kIterations = 12;
    for (u32 iteration = 0; iteration < kIterations; ++iteration)
    {
        TestContext context;
        context.blockFrame = true;
        RenderThreadDesc desc = MakeDesc(context);
        desc.renderFrame = BlockingFrame;
        auto created = RenderThread::Create(desc);
        CG_REQUIRE(created.IsOk());
        RenderThread renderThread = std::move(created.Value());
        context.renderThread = &renderThread;

        const Byte value[]{static_cast<Byte>(iteration)};
        CG_REQUIRE(renderThread.SubmitFrame(Span<const Byte>{value, 1}).IsOk());
        CG_REQUIRE(renderThread.RequestResize(iteration, iteration + 1U).IsOk());
        std::thread release(
            [&context]
            {
                context.frameStarted.wait(false, std::memory_order_acquire);
                context.releaseFrame.store(true, std::memory_order_release);
                context.releaseFrame.notify_all();
            });
        CG_REQUIRE(renderThread.Shutdown().IsOk());
        release.join();

        CG_EXPECT_EQ(context.events[context.eventCount - 1U], kShutdownEvent);
        CG_EXPECT_EQ(context.events[context.eventCount - 2U], 1000U + iteration + iteration + 1U);
        CG_EXPECT_TRUE(context.callbacksOnRenderOwner);
    }
}

CG_TEST(renderer_render_thread_surfaces_callback_errors_once)
{
    TestContext context;
    RenderThreadDesc desc = MakeDesc(context);
    desc.renderFrame = FailFrame;
    auto created = RenderThread::Create(desc);
    CG_REQUIRE(created.IsOk());
    RenderThread renderThread = std::move(created.Value());
    context.renderThread = &renderThread;

    const Byte value[]{static_cast<Byte>(1)};
    CG_REQUIRE(renderThread.SubmitFrame(Span<const Byte>{value, 1}).IsOk());
    const Status<Error> first = renderThread.WaitIdle();
    CG_EXPECT_TRUE(first.IsError());
    if (first.IsError())
    {
        CG_EXPECT_EQ(first.Error().code, ErrorCode::DeviceLost);
        CG_EXPECT_STREQ(first.Error().message, "test render callback failure");
    }
    CG_EXPECT_TRUE(renderThread.WaitIdle().IsOk());
    CG_EXPECT_TRUE(renderThread.Shutdown().IsOk());
}

CG_TEST(renderer_render_thread_rejects_bad_input_and_nonproducer_calls)
{
    TestContext context;
    RenderThreadDesc desc = MakeDesc(context);
    desc.maxFrameDataBytes = 1;
    auto created = RenderThread::Create(desc);
    CG_REQUIRE(created.IsOk());
    RenderThread renderThread = std::move(created.Value());

    const Byte oversized[]{static_cast<Byte>(1), static_cast<Byte>(2)};
    const auto tooLarge = renderThread.SubmitFrame(Span<const Byte>{oversized, 2});
    CG_EXPECT_TRUE(tooLarge.IsError());
    if (tooLarge.IsError())
    {
        CG_EXPECT_EQ(tooLarge.Error().code, ErrorCode::OutOfRange);
    }

    ErrorCode otherThreadError = ErrorCode::Ok;
    std::thread otherThread(
        [&]
        {
            const Byte value[]{static_cast<Byte>(1)};
            const auto result = renderThread.SubmitFrame(Span<const Byte>{value, 1});
            if (result.IsError())
            {
                otherThreadError = result.Error().code;
            }
        });
    otherThread.join();
    CG_EXPECT_EQ(otherThreadError, ErrorCode::InvalidArgument);
    CG_REQUIRE(renderThread.Shutdown().IsOk());

    RenderThreadDesc invalid{};
    invalid.allocator = &context.allocator;
    auto missingFrameCallback = RenderThread::Create(invalid);
    CG_EXPECT_TRUE(missingFrameCallback.IsError());
    if (missingFrameCallback.IsError())
    {
        CG_EXPECT_EQ(missingFrameCallback.Error().code, ErrorCode::InvalidArgument);
    }
}

CG_TEST(renderer_render_thread_initialization_failure_is_returned)
{
    TestContext context;
    RenderThreadDesc desc = MakeDesc(context);
    desc.initialize = [](void*) noexcept -> Status<Error>
    { return ErrorStatus(ErrorCode::Unsupported, "test initialization failure"); };
    auto created = RenderThread::Create(desc);
    CG_EXPECT_TRUE(created.IsError());
    if (created.IsError())
    {
        CG_EXPECT_EQ(created.Error().code, ErrorCode::Unsupported);
        CG_EXPECT_STREQ(created.Error().message, "test initialization failure");
    }
    CG_EXPECT_EQ(context.eventCount, u32{1});
    CG_EXPECT_EQ(context.events[0], kShutdownEvent);
}

CG_TEST(renderer_render_thread_default_and_maximum_frames_are_validated)
{
    TestContext context;
    RenderThreadDesc desc = MakeDesc(context);
    auto created = RenderThread::Create(desc);
    CG_REQUIRE(created.IsOk());
    RenderThread renderThread = std::move(created.Value());
    CG_EXPECT_EQ(renderThread.FramesInFlight(), u32{2});
    CG_REQUIRE(renderThread.Shutdown().IsOk());

    desc.framesInFlight = cyber::rhi::kMaxFramesInFlight;
    auto maximum = RenderThread::Create(desc);
    CG_REQUIRE(maximum.IsOk());
    CG_EXPECT_EQ(maximum.Value().FramesInFlight(), cyber::rhi::kMaxFramesInFlight);
    CG_REQUIRE(maximum.Value().Shutdown().IsOk());

    desc.framesInFlight = cyber::rhi::kMaxFramesInFlight + 1U;
    auto invalid = RenderThread::Create(desc);
    CG_EXPECT_TRUE(invalid.IsError());
    if (invalid.IsError())
    {
        CG_EXPECT_EQ(invalid.Error().code, ErrorCode::OutOfRange);
    }
}
