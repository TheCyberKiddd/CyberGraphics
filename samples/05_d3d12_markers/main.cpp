#include <cyber/core/error.h>
#include <cyber/core/log.h>
#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/platform/platform.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/submission.h>
#include <cyber/rhi/swapchain.h>

#include <cstdio>
#include <utility>

using cyber::core::Error;
using cyber::core::u32;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::platform::Key;
using cyber::platform::Window;
using cyber::platform::WindowDesc;
using cyber::platform::WindowEvent;
using cyber::platform::WindowEventType;
using cyber::rhi::Backend;
using cyber::rhi::BufferBarrier;
using cyber::rhi::ClearColorValue;
using cyber::rhi::CommandList;
using cyber::rhi::CommandListDesc;
using cyber::rhi::CommandListHandle;
using cyber::rhi::D3D12Backend;
using cyber::rhi::Device;
using cyber::rhi::DeviceDesc;
using cyber::rhi::Format;
using cyber::rhi::QueueKind;
using cyber::rhi::ResourceState;
using cyber::rhi::SubmitDesc;
using cyber::rhi::SubresourceRange;
using cyber::rhi::SwapChain;
using cyber::rhi::SwapChainDesc;
using cyber::rhi::TextureBarrier;
using cyber::rhi::ValidationLevel;

namespace
{

class ConsoleLogSink final : public cyber::core::LogSink
{
public:
    ~ConsoleLogSink() override
    {
        (void)cyber::core::DefaultLogger().RemoveSink(this);
    }

    void Write(const cyber::core::LogRecord& record) noexcept override
    {
        std::printf("[%s][%s] %s\n", cyber::core::LogLevelName(record.level),
                    cyber::core::LogChannelName(record.channel), record.message);
    }
};

void PrintFailure(const char* operation, const Error& error) noexcept
{
    std::printf("%s failed: %s (%s)\n", operation, cyber::core::ErrorCodeName(error.code),
                error.message != nullptr ? error.message : "no diagnostic");
}

void PrintFrameStep(bool enabled, const char* step) noexcept
{
#if defined(CG_DEBUG) && CG_DEBUG
    if (enabled)
    {
        std::printf("[frame] %s\n", step);
    }
#else
    (void)enabled;
    (void)step;
#endif
}

[[nodiscard]] bool DrawFrame(Device& device, SwapChain& swapChain, bool trace) noexcept
{
    auto acquired = swapChain.AcquireNextImage();
    if (!acquired)
    {
        if (acquired.Error().code == cyber::core::ErrorCode::NotFound)
        {
            return true;
        }
        PrintFailure("AcquireNextImage", acquired.Error());
        return false;
    }
    PrintFrameStep(trace, "image acquired");

    CommandListDesc listDesc{};
    listDesc.label = "Marker sample graphics list";
    PrintFrameStep(trace, "creating command list");
    auto createdList = device.CreateCommandList(listDesc);
    if (!createdList)
    {
        PrintFailure("CreateCommandList", createdList.Error());
        return false;
    }
    PrintFrameStep(trace, "command list created");
    CommandList list = std::move(createdList.Value());
    const auto begun = list.Begin();
    if (!begun)
    {
        PrintFailure("Begin command list", begun.Error());
        return false;
    }
    PrintFrameStep(trace, "command list begun");

    const TextureBarrier toRenderTarget{acquired.Value().texture, SubresourceRange{},
                                        ResourceState::Present, ResourceState::RenderTarget};
    const auto transitioned =
        list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(&toRenderTarget, 1));
    if (!transitioned)
    {
        PrintFailure("Transition backbuffer to render target", transitioned.Error());
        return false;
    }
    PrintFrameStep(trace, "backbuffer transitioned");

    const auto beganPass = list.BeginDebugLabel("Clear Pass");
    if (!beganPass)
    {
        PrintFailure("Begin clear pass label", beganPass.Error());
        return false;
    }
    PrintFrameStep(trace, "clear pass label begun");

    const auto beganClear = list.BeginDebugLabel("Clear backbuffer");
    if (!beganClear)
    {
        PrintFailure("Begin backbuffer clear label", beganClear.Error());
        (void)list.EndDebugLabel();
        return false;
    }
    const ClearColorValue clearValue = ClearColorValue::Of(0.025f, 0.10f, 0.28f, 1.0f);
    const auto cleared = list.ClearColorTexture(acquired.Value().texture, clearValue);
    if (!cleared)
    {
        PrintFailure("Clear backbuffer", cleared.Error());
        (void)list.EndDebugLabel();
        (void)list.EndDebugLabel();
        return false;
    }
    PrintFrameStep(trace, "backbuffer cleared");

    const auto endedClear = list.EndDebugLabel();
    if (!endedClear)
    {
        PrintFailure("End backbuffer clear label", endedClear.Error());
        (void)list.EndDebugLabel();
        return false;
    }
    const auto endedPass = list.EndDebugLabel();
    if (!endedPass)
    {
        PrintFailure("End clear pass label", endedPass.Error());
        return false;
    }
    PrintFrameStep(trace, "clear pass label ended");

    const TextureBarrier toPresent{acquired.Value().texture, SubresourceRange{},
                                   ResourceState::RenderTarget, ResourceState::Present};
    const auto readyToPresent =
        list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(&toPresent, 1));
    if (!readyToPresent)
    {
        PrintFailure("Transition backbuffer to present", readyToPresent.Error());
        return false;
    }
    PrintFrameStep(trace, "backbuffer ready to present");
    const auto ended = list.End();
    if (!ended)
    {
        PrintFailure("End command list", ended.Error());
        return false;
    }
    PrintFrameStep(trace, "command list ended");

    const CommandListHandle handle = list.Handle();
    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const CommandListHandle>(&handle, 1);
    submit.label = "Marker sample frame";
    const auto submitted = device.Submit(submit);
    if (!submitted)
    {
        PrintFailure("Submit", submitted.Error());
        return false;
    }
    PrintFrameStep(trace, "command list submitted");

    const auto presented = swapChain.Present();
    if (!presented)
    {
        PrintFailure("Present", presented.Error());
        return false;
    }
    PrintFrameStep(trace, "frame presented");
    const auto idle = device.WaitIdle();
    if (!idle)
    {
        PrintFailure("WaitIdle", idle.Error());
        return false;
    }
    PrintFrameStep(trace, "device idle");
    return true;
}

}  // namespace

int main()
{
    (void)std::setvbuf(stdout, nullptr, _IONBF, 0);
    ConsoleLogSink logSink;
    if (!cyber::core::DefaultLogger().AddSink(&logSink))
    {
        std::printf("Could not register the sample log sink.\n");
        return 1;
    }

    if (!cyber::platform::IsWindowSystemSupported())
    {
        std::printf("D3D12 marker sample requires a window system.\n");
        return 0;
    }

    const Backend* backend = D3D12Backend();
    if (backend == nullptr)
    {
        std::printf("D3D12 marker sample was built without the D3D12 backend.\n");
        return 0;
    }

    HeapAllocator heap;
    WindowDesc windowDesc{};
    windowDesc.title = "CyberGraphics - D3D12 GPU markers";
    windowDesc.width = 960;
    windowDesc.height = 540;
    auto createdWindow = Window::Create(windowDesc, heap);
    if (!createdWindow)
    {
        PrintFailure("Window::Create", createdWindow.Error());
        return 1;
    }
    Window window = std::move(createdWindow.Value());

    DeviceDesc deviceDesc{};
    deviceDesc.backend = backend;
#if defined(CG_DEBUG) && CG_DEBUG
    deviceDesc.validation = ValidationLevel::Standard;
#else
    deviceDesc.validation = ValidationLevel::Off;
#endif
    const u32 adapterCount = backend->enumerateAdapters(nullptr, 0);
    if (adapterCount == 0)
    {
        std::printf("No D3D12 adapter is available.\n");
        return 1;
    }
    deviceDesc.adapterIndex = 0;
    auto createdDevice = Device::Create(deviceDesc, heap);
    if (!createdDevice)
    {
        PrintFailure("Device::Create", createdDevice.Error());
        return 1;
    }
    Device device = std::move(createdDevice.Value());

    SwapChainDesc swapChainDesc{};
    swapChainDesc.surface.nativeWindow = window.NativeHandle();
    swapChainDesc.surface.width = static_cast<u32>(window.Width());
    swapChainDesc.surface.height = static_cast<u32>(window.Height());
    swapChainDesc.format = Format::BGRA8Unorm;
    swapChainDesc.imageCount = 2;
    swapChainDesc.label = "Marker sample swapchain";
    auto createdSwapChain = device.CreateSwapChain(swapChainDesc);
    if (!createdSwapChain)
    {
        PrintFailure("CreateSwapChain", createdSwapChain.Error());
        return 1;
    }
    SwapChain swapChain = std::move(createdSwapChain.Value());
    std::printf("Capture a presented frame in PIX or RenderDoc. Press Escape to close.\n");

    bool traceFrame = true;
    while (window.IsOpen())
    {
        window.BeginFrame();
        window.PollEvents();

        WindowEvent event{};
        while (window.PollEvent(event))
        {
            if (event.type == WindowEventType::Close ||
                (event.type == WindowEventType::KeyDown && event.key == Key::Escape))
            {
                window.RequestClose();
            }
            else if (event.type == WindowEventType::Resize && event.width > 0 && event.height > 0)
            {
                const auto info = swapChain.Info();
                if (info.width != static_cast<u32>(event.width) ||
                    info.height != static_cast<u32>(event.height))
                {
                    const auto resized = swapChain.Resize(static_cast<u32>(event.width),
                                                          static_cast<u32>(event.height));
                    if (!resized)
                    {
                        PrintFailure("Resize", resized.Error());
                        return 1;
                    }
                }
            }
        }
        if (!window.IsOpen())
        {
            break;
        }
        if (!DrawFrame(device, swapChain, traceFrame))
        {
            return 1;
        }
        traceFrame = false;
    }

    return 0;
}
