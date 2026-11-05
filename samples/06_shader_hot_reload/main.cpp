#include <cyber/core/log.h>
#include <cyber/core/types.h>
#include <cyber/jobs/job_system.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>
#include <cyber/platform/filesystem.h>
#include <cyber/platform/platform.h>
#include <cyber/platform/window.h>
#include <cyber/renderer/hot_pipeline.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/submission.h>
#include <cyber/rhi/swapchain.h>
#include <cyber/shaders/cache.h>
#include <cyber/shaders/compiler.h>
#include <cyber/shaders/library.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>

using cyber::core::Error;
using cyber::core::u32;
using cyber::core::u64;
using cyber::jobs::JobSystem;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::String;
using cyber::memory::StringView;
using cyber::platform::Key;
using cyber::platform::Window;
using cyber::platform::WindowDesc;
using cyber::platform::WindowEvent;
using cyber::platform::WindowEventType;
using cyber::renderer::HotGraphicsPipeline;
using cyber::renderer::HotGraphicsPipelineDesc;
using cyber::rhi::Backend;
using cyber::rhi::BufferBarrier;
using cyber::rhi::BufferCopy;
using cyber::rhi::BufferDesc;
using cyber::rhi::BufferHandle;
using cyber::rhi::BufferUsage;
using cyber::rhi::ClearColorValue;
using cyber::rhi::ColorAttachment;
using cyber::rhi::ColorAttachmentState;
using cyber::rhi::CommandList;
using cyber::rhi::CommandListDesc;
using cyber::rhi::CommandListHandle;
using cyber::rhi::Device;
using cyber::rhi::DeviceDesc;
using cyber::rhi::Format;
using cyber::rhi::LoadOp;
using cyber::rhi::PresentMode;
using cyber::rhi::QueueKind;
using cyber::rhi::RenderPassDesc;
using cyber::rhi::ResourceState;
using cyber::rhi::ScissorRect;
using cyber::rhi::StoreOp;
using cyber::rhi::SubmitDesc;
using cyber::rhi::SubresourceRange;
using cyber::rhi::SwapChain;
using cyber::rhi::SwapChainDesc;
using cyber::rhi::TextureBarrier;
using cyber::rhi::TextureHandle;
using cyber::rhi::Viewport;
using cyber::shaders::CompiledShader;
using cyber::shaders::ShaderCache;
using cyber::shaders::ShaderEvent;
using cyber::shaders::ShaderEventKind;
using cyber::shaders::ShaderId;
using cyber::shaders::ShaderLibrary;
using cyber::shaders::ShaderLibraryDesc;

namespace
{

constexpr char kVertexSource[] = "shader vertex HotReload {\n"
                                 "    input float2 position @location(0);\n"
                                 "    output float4 clipPosition @builtin(position);\n"
                                 "    output.clipPosition = float4(input.position, 0.5, 1.0);\n"
                                 "}\n";

constexpr char kPixelSource[] = "shader pixel HotReload {\n"
                                "    keyword TINT;\n"
                                "    input float4 position @builtin(position);\n"
                                "    when TINT {\n"
                                "        return float4(0.8, 0.2, 0.9, 1.0);\n"
                                "    } else {\n"
                                "        return float4(0.2, 0.55, 0.9, 1.0);\n"
                                "    }\n"
                                "}\n";

constexpr char kPixelReloadSource[] = "shader pixel HotReload {\n"
                                      "    keyword TINT;\n"
                                      "    input float4 position @builtin(position);\n"
                                      "    when TINT {\n"
                                      "        return float4(0.0, 0.8, 0.25, 1.0);\n"
                                      "    } else {\n"
                                      "        return float4(0.0, 0.8, 0.2, 1.0);\n"
                                      "    }\n"
                                      "}\n";

constexpr char kPixelSyntaxError[] = "shader pixel HotReload {\n"
                                     "    input float4 position @builtin(position);\n"
                                     "    return float4(1.0, 0.0, 0.0, 1.0)\n"
                                     "}\n";

constexpr float kTriangle[] = {-0.75f, -0.65f, 0.75f, -0.65f, 0.0f, 0.75f};

class ConsoleLogSink final : public cyber::core::LogSink
{
public:
    ~ConsoleLogSink() override
    {
        static_cast<void>(cyber::core::DefaultLogger().RemoveSink(this));
    }

    void Write(const cyber::core::LogRecord& record) noexcept override
    {
        std::printf("[%s][%s] %s\n", cyber::core::LogLevelName(record.level),
                    cyber::core::LogChannelName(record.channel), record.message);
    }
};

enum class SelfTestStage : u32
{
    WaitingForInitialPipeline = 0,
    WaitingForValidReload,
    WaitingForSyntaxError,
    WaitingForRestore,
    Complete,
    Failed,
};

struct SelfTest
{
    SelfTestStage stage = SelfTestStage::WaitingForInitialPipeline;
    u64 startedAt = 0;
    u32 targetGeneration = 0;
    u64 targetFailures = 0;
    u32 unchangedGeneration = 0;
    cyber::rhi::PipelineHandle unchangedPipeline{};
};

[[nodiscard]] u64 NowNanoseconds() noexcept
{
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

void PrintFailure(const char* operation, const Error& error) noexcept
{
    std::printf("%s failed: %s (%s)\n", operation, cyber::core::ErrorCodeName(error.code),
                error.message != nullptr ? error.message : "no diagnostic");
}

[[nodiscard]] bool MakePath(String& path, StringView directory, const char* file) noexcept
{
    return path.TryAssign(directory) && path.TryAppend('/') && path.TryAppend(file);
}

[[nodiscard]] bool WriteText(const String& path, const char* text) noexcept
{
    const StringView view(text);
    return cyber::platform::WriteFile(
               path.CStr(),
               Span<const Byte>(reinterpret_cast<const Byte*>(view.Data()), view.Size()))
        .IsOk();
}

[[nodiscard]] cyber::core::Result<CompiledShader, Error>
LoadAndCompile(const String& path, HeapAllocator& allocator) noexcept
{
    auto file = cyber::platform::ReadFile(path.CStr(), allocator);
    if (!file)
    {
        return cyber::core::Result<CompiledShader, Error>::Err(file.Error());
    }
    const auto& bytes = file.Value();
    const StringView source(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
    auto compiled = cyber::shaders::CompileHlsl(path.View(), source,
                                                cyber::shaders::CompileOptions{}, allocator);
    if (!compiled)
    {
        const auto& diagnostic = compiled.Error();
        std::printf("%s:%u:%u: %s\n", path.CStr(), diagnostic.line, diagnostic.column,
                    diagnostic.message != nullptr ? diagnostic.message : "compile failed");
        return cyber::core::Result<CompiledShader, Error>::Err(
            Error{cyber::core::ErrorCode::ParseError, "shader source compilation failed"});
    }
    return cyber::core::Result<CompiledShader, Error>::Ok(std::move(compiled.Value()));
}

[[nodiscard]] bool WriteReport(ShaderLibrary& library, const String& reportPath,
                               HeapAllocator& allocator) noexcept
{
    String report(allocator);
    if (!library.WritePermutationReport(report))
    {
        std::printf("Could not allocate the shader permutation report.\n");
        return false;
    }
    const StringView view = report.View();
    const bool written =
        cyber::platform::WriteFile(
            reportPath.CStr(),
            Span<const Byte>(reinterpret_cast<const Byte*>(view.Data()), view.Size()))
            .IsOk();
    if (written)
    {
        std::printf("Permutation report: %s\n", reportPath.CStr());
    }
    return written;
}

[[nodiscard]] bool DrawFrame(Device& device, SwapChain& swapChain, HotGraphicsPipeline& pipeline,
                             BufferHandle vertices, u32 width, u32 height) noexcept
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

    CommandListDesc listDesc{};
    listDesc.label = "Shader hot-reload sample frame";
    auto createdList = device.CreateCommandList(listDesc);
    if (!createdList)
    {
        PrintFailure("CreateCommandList", createdList.Error());
        return false;
    }
    CommandList list = std::move(createdList.Value());
    if (!list.Begin().IsOk())
    {
        std::printf("Could not begin the command list.\n");
        return false;
    }

    const TextureHandle backbuffer = acquired.Value().texture;
    const TextureBarrier toRenderTarget{backbuffer, SubresourceRange{}, ResourceState::Present,
                                        ResourceState::RenderTarget};
    if (!list.Barrier(Span<const cyber::rhi::BufferBarrier>{},
                      Span<const TextureBarrier>(&toRenderTarget, 1))
             .IsOk())
    {
        std::printf("Could not transition the backbuffer for drawing.\n");
        return false;
    }

    ColorAttachment color{};
    color.view = acquired.Value().view;
    color.loadOp = LoadOp::Clear;
    color.storeOp = StoreOp::Store;
    color.clearValue = ClearColorValue::Of(0.025f, 0.035f, 0.07f, 1.0f);
    RenderPassDesc pass{};
    pass.colorAttachments = Span<const ColorAttachment>(&color, 1);
    if (!list.BeginRenderPass(pass).IsOk() ||
        !list.SetViewport(Viewport::Of(static_cast<float>(width), static_cast<float>(height)))
             .IsOk() ||
        !list.SetScissor(ScissorRect{0, 0, width, height}).IsOk())
    {
        std::printf("Could not begin the backbuffer render pass.\n");
        return false;
    }

    const auto current = pipeline.Current();
    if (current.IsValid() &&
        (!list.SetPipeline(current).IsOk() || !list.SetVertexBuffer(0, vertices).IsOk() ||
         !list.Draw(cyber::rhi::DrawArguments{3, 1, 0, 0}).IsOk()))
    {
        std::printf("Could not draw the hot-reload triangle.\n");
        return false;
    }
    if (!list.EndRenderPass().IsOk())
    {
        std::printf("Could not end the backbuffer render pass.\n");
        return false;
    }

    const TextureBarrier toPresent{backbuffer, SubresourceRange{}, ResourceState::RenderTarget,
                                   ResourceState::Present};
    if (!list.Barrier(Span<const cyber::rhi::BufferBarrier>{},
                      Span<const TextureBarrier>(&toPresent, 1))
             .IsOk() ||
        !list.End().IsOk())
    {
        std::printf("Could not finish the command list.\n");
        return false;
    }

    const CommandListHandle handle = list.Handle();
    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const CommandListHandle>(&handle, 1);
    submit.label = "Shader hot-reload sample frame";
    if (auto submitted = device.Submit(submit); !submitted)
    {
        PrintFailure("Submit", submitted.Error());
        return false;
    }
    if (auto presented = swapChain.Present(); !presented)
    {
        PrintFailure("Present", presented.Error());
        return false;
    }
    if (auto idle = device.WaitIdle(); !idle)
    {
        PrintFailure("WaitIdle", idle.Error());
        return false;
    }
    return true;
}

[[nodiscard]] bool StartValidEdit(SelfTest& test, const String& pixelPath,
                                  const HotGraphicsPipeline& pipeline) noexcept
{
    if (!WriteText(pixelPath, kPixelReloadSource))
    {
        std::printf("Could not write the valid self-test shader edit.\n");
        test.stage = SelfTestStage::Failed;
        return false;
    }
    test.targetGeneration = pipeline.Generation() + 1;
    test.startedAt = NowNanoseconds();
    test.stage = SelfTestStage::WaitingForValidReload;
    return true;
}

[[nodiscard]] bool UpdateSelfTest(SelfTest& test, ShaderLibrary& library, ShaderId pixelId,
                                  HotGraphicsPipeline& pipeline, const String& pixelPath,
                                  const String& reportPath, HeapAllocator& allocator) noexcept
{
    const u64 now = NowNanoseconds();
    if (test.stage == SelfTestStage::WaitingForInitialPipeline)
    {
        if (pipeline.Generation() > 0 && !pipeline.IsBuilding())
        {
            return StartValidEdit(test, pixelPath, pipeline);
        }
        return true;
    }
    if (test.stage == SelfTestStage::WaitingForValidReload)
    {
        if (pipeline.Generation() >= test.targetGeneration && !pipeline.IsBuilding())
        {
            const u64 elapsed = (now - test.startedAt) / 1000000ULL;
            std::printf("Valid shader edit applied in %llu ms.\n",
                        static_cast<unsigned long long>(elapsed));
            if (elapsed > 1000)
            {
                std::printf("Valid shader edit exceeded the 1000 ms target.\n");
                test.stage = SelfTestStage::Failed;
                return false;
            }
            test.targetFailures = library.Stats(pixelId).failures + 1;
            test.unchangedGeneration = pipeline.Generation();
            test.unchangedPipeline = pipeline.Current();
            if (!WriteText(pixelPath, kPixelSyntaxError))
            {
                std::printf("Could not write the syntax-error self-test edit.\n");
                test.stage = SelfTestStage::Failed;
                return false;
            }
            test.startedAt = now;
            test.stage = SelfTestStage::WaitingForSyntaxError;
            return true;
        }
    }
    else if (test.stage == SelfTestStage::WaitingForSyntaxError)
    {
        if (library.Stats(pixelId).failures >= test.targetFailures && !pipeline.IsBuilding())
        {
            const u64 elapsed = (now - test.startedAt) / 1000000ULL;
            if (pipeline.Generation() != test.unchangedGeneration ||
                pipeline.Current() != test.unchangedPipeline)
            {
                std::printf("The syntax error replaced the last working pipeline.\n");
                test.stage = SelfTestStage::Failed;
                return false;
            }
            std::printf("Syntax error rejected in %llu ms; previous pipeline preserved.\n",
                        static_cast<unsigned long long>(elapsed));
            if (!WriteText(pixelPath, kPixelReloadSource))
            {
                std::printf("Could not restore the self-test shader.\n");
                test.stage = SelfTestStage::Failed;
                return false;
            }
            test.targetGeneration = pipeline.Generation() + 1;
            test.startedAt = now;
            test.stage = SelfTestStage::WaitingForRestore;
            return true;
        }
    }
    else if (test.stage == SelfTestStage::WaitingForRestore &&
             pipeline.Generation() >= test.targetGeneration && !pipeline.IsBuilding())
    {
        const u64 elapsed = (now - test.startedAt) / 1000000ULL;
        std::printf("Restored shader applied in %llu ms.\n",
                    static_cast<unsigned long long>(elapsed));
        if (elapsed > 1000 || !WriteReport(library, reportPath, allocator))
        {
            test.stage = SelfTestStage::Failed;
            return false;
        }
        test.stage = SelfTestStage::Complete;
    }

    if ((test.stage == SelfTestStage::WaitingForValidReload ||
         test.stage == SelfTestStage::WaitingForSyntaxError ||
         test.stage == SelfTestStage::WaitingForRestore) &&
        now - test.startedAt > 1000000000ULL)
    {
        std::printf("Shader self-test stage exceeded the 1000 ms target.\n");
        test.stage = SelfTestStage::Failed;
        return false;
    }
    return test.stage != SelfTestStage::Failed;
}

[[nodiscard]] bool ParseArguments(int argc, char** argv, bool& selfTest) noexcept
{
    selfTest = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--self-test") == 0)
        {
            selfTest = true;
        }
        else
        {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    bool selfTestEnabled = false;
    if (!ParseArguments(argc, argv, selfTestEnabled))
    {
        std::printf("usage: cyber_sample_shader_hot_reload [--self-test]\n");
        return 2;
    }

    ConsoleLogSink logSink;
    if (!cyber::core::DefaultLogger().AddSink(&logSink))
    {
        std::printf("Could not register the sample log sink.\n");
        return 1;
    }
    if (!cyber::platform::IsWindowSystemSupported())
    {
        std::printf("The hot-reload sample requires a window system.\n");
        return 1;
    }

    const Backend* backend = cyber::rhi::D3D12Backend();
    if (backend == nullptr || backend->enumerateAdapters(nullptr, 0) == 0)
    {
        std::printf("No D3D12 backend or adapter is available.\n");
        return 1;
    }

    HeapAllocator heap;
    WindowDesc windowDesc{};
    windowDesc.title = "CyberGraphics - shader hot reload";
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
    swapChainDesc.presentMode = PresentMode::Fifo;
    swapChainDesc.label = "Shader hot-reload sample swapchain";
    auto createdSwapChain = device.CreateSwapChain(swapChainDesc);
    if (!createdSwapChain)
    {
        PrintFailure("CreateSwapChain", createdSwapChain.Error());
        return 1;
    }
    SwapChain swapChain = std::move(createdSwapChain.Value());

    JobSystem::Desc jobsDesc{};
    jobsDesc.workerCount = 2;
    auto createdJobs = JobSystem::Create(jobsDesc, heap);
    if (!createdJobs)
    {
        PrintFailure("JobSystem::Create", createdJobs.Error());
        return 1;
    }
    JobSystem jobs = std::move(createdJobs.Value());

    auto executableDirectory = cyber::platform::ExecutableDirectory(heap);
    if (!executableDirectory)
    {
        PrintFailure("ExecutableDirectory", executableDirectory.Error());
        return 1;
    }
    String shaderDirectory(heap);
    String vertexPath(heap);
    String pixelPath(heap);
    String cacheDirectory(heap);
    String reportPath(heap);
    if (selfTestEnabled)
    {
        if (!MakePath(shaderDirectory, executableDirectory.Value().View(), "shader-self-test") ||
            !cyber::platform::CreateDirectories(shaderDirectory.CStr()).IsOk() ||
            !MakePath(vertexPath, shaderDirectory.View(), "triangle_vertex.cgs") ||
            !MakePath(pixelPath, shaderDirectory.View(), "triangle_pixel.cgs") ||
            !WriteText(vertexPath, kVertexSource) || !WriteText(pixelPath, kPixelSource) ||
            !MakePath(reportPath, shaderDirectory.View(), "shader-permutation-report.txt"))
        {
            std::printf("Could not prepare the self-test shader files.\n");
            return 1;
        }
    }
    else if (!MakePath(shaderDirectory, executableDirectory.Value().View(), "shaders") ||
             !MakePath(vertexPath, shaderDirectory.View(), "triangle_vertex.cgs") ||
             !MakePath(pixelPath, shaderDirectory.View(), "triangle_pixel.cgs") ||
             !MakePath(reportPath, executableDirectory.Value().View(),
                       "shader-permutation-report.txt"))
    {
        std::printf("Could not create the shader paths.\n");
        return 1;
    }

    if (!MakePath(cacheDirectory, executableDirectory.Value().View(), "shader-cache") ||
        !cyber::platform::CreateDirectories(cacheDirectory.CStr()).IsOk())
    {
        std::printf("Could not create the shader cache directory.\n");
        return 1;
    }
    auto openedCache = ShaderCache::Open(cacheDirectory.View());
    if (!openedCache)
    {
        PrintFailure("ShaderCache::Open", openedCache.Error());
        return 1;
    }
    ShaderCache cache = std::move(openedCache.Value());

    ShaderLibraryDesc libraryDesc{};
    libraryDesc.jobs = &jobs;
    libraryDesc.cache = &cache;
    libraryDesc.maxPermutationsPerShader = 2;
    libraryDesc.pollIntervalMilliseconds = 50;
    libraryDesc.watchFiles = true;
    auto createdLibrary = ShaderLibrary::Create(libraryDesc, heap);
    if (!createdLibrary)
    {
        PrintFailure("ShaderLibrary::Create", createdLibrary.Error());
        return 1;
    }
    ShaderLibrary library = std::move(createdLibrary.Value());

    auto vertexIdResult = library.Register(vertexPath.View());
    auto pixelIdResult = library.Register(pixelPath.View());
    if (!vertexIdResult || !pixelIdResult)
    {
        if (!vertexIdResult)
        {
            PrintFailure("Register vertex shader", vertexIdResult.Error());
        }
        if (!pixelIdResult)
        {
            PrintFailure("Register pixel shader", pixelIdResult.Error());
        }
        return 1;
    }
    const ShaderId vertexId = vertexIdResult.Value();
    const ShaderId pixelId = pixelIdResult.Value();
    const StringView tintKeyword[] = {"TINT"};
    auto tintHashResult =
        library.RequestPermutation(pixelId, Span<const StringView>(tintKeyword, 1));
    if (!tintHashResult)
    {
        PrintFailure("RequestPermutation", tintHashResult.Error());
        return 1;
    }
    const u64 tintHash = tintHashResult.Value();

    auto fallbackVertexResult = LoadAndCompile(vertexPath, heap);
    auto fallbackPixelResult = LoadAndCompile(pixelPath, heap);
    if (!fallbackVertexResult || !fallbackPixelResult)
    {
        if (!fallbackVertexResult)
        {
            PrintFailure("Compile fallback vertex shader", fallbackVertexResult.Error());
        }
        if (!fallbackPixelResult)
        {
            PrintFailure("Compile fallback pixel shader", fallbackPixelResult.Error());
        }
        return 1;
    }
    CompiledShader fallbackVertex = std::move(fallbackVertexResult.Value());
    CompiledShader fallbackPixel = std::move(fallbackPixelResult.Value());
    ColorAttachmentState colorState{};
    colorState.format = Format::BGRA8Unorm;
    HotGraphicsPipelineDesc fallbackDesc{};
    fallbackDesc.device = &device;
    fallbackDesc.state.colorAttachments = Span<const ColorAttachmentState>(&colorState, 1);
    fallbackDesc.state.rasterizer.cullMode = cyber::rhi::CullMode::None;
    fallbackDesc.state.depthStencil.depthTestEnabled = false;
    fallbackDesc.state.depthStencil.depthWriteEnabled = false;
    fallbackDesc.label = "Shader hot-reload fallback";
    auto createdFallback = HotGraphicsPipeline::Create(fallbackDesc, heap);
    if (!createdFallback)
    {
        PrintFailure("Create fallback pipeline", createdFallback.Error());
        return 1;
    }
    HotGraphicsPipeline fallbackPipeline = std::move(createdFallback.Value());
    if (!fallbackPipeline.Rebuild(fallbackVertex, fallbackPixel).IsOk())
    {
        std::printf("Could not build the fallback pipeline.\n");
        return 1;
    }
    fallbackPipeline.BeginFrame(0);
    if (!fallbackPipeline.HasBuilt())
    {
        const Error* error = fallbackPipeline.LastError();
        if (error != nullptr)
        {
            PrintFailure("Fallback pipeline build", *error);
        }
        return 1;
    }

    HotGraphicsPipelineDesc hotDesc = fallbackDesc;
    hotDesc.jobs = &jobs;
    hotDesc.fallback = fallbackPipeline.Current();
    hotDesc.label = "Shader hot-reload pipeline";
    auto createdPipeline = HotGraphicsPipeline::Create(hotDesc, heap);
    if (!createdPipeline)
    {
        PrintFailure("HotGraphicsPipeline::Create", createdPipeline.Error());
        return 1;
    }
    HotGraphicsPipeline pipeline = std::move(createdPipeline.Value());

    BufferDesc vertexBufferDesc{};
    vertexBufferDesc.usage = BufferUsage::Vertex | BufferUsage::CopyDestination;
    vertexBufferDesc.sizeBytes = sizeof(kTriangle);
    auto createdVertices = device.CreateBuffer(vertexBufferDesc);
    if (!createdVertices)
    {
        PrintFailure("CreateBuffer", createdVertices.Error());
        return 1;
    }
    const BufferHandle vertices = createdVertices.Value();
    BufferDesc stagingDesc{};
    stagingDesc.usage = BufferUsage::CopySource | BufferUsage::HostVisible;
    stagingDesc.sizeBytes = sizeof(kTriangle);
    auto createdStaging = device.CreateBuffer(stagingDesc);
    if (!createdStaging)
    {
        PrintFailure("CreateBuffer staging", createdStaging.Error());
        return 1;
    }
    const BufferHandle staging = createdStaging.Value();
    if (auto written = device.WriteBuffer(
            staging, 0,
            Span<const Byte>(reinterpret_cast<const Byte*>(kTriangle), sizeof(kTriangle)));
        !written)
    {
        PrintFailure("WriteBuffer staging", written.Error());
        return 1;
    }
    CommandListDesc uploadListDesc{};
    uploadListDesc.label = "Shader hot-reload vertex upload";
    auto createdUploadList = device.CreateCommandList(uploadListDesc);
    if (!createdUploadList)
    {
        PrintFailure("CreateCommandList upload", createdUploadList.Error());
        return 1;
    }
    CommandList uploadList = std::move(createdUploadList.Value());
    if (!uploadList.Begin().IsOk())
    {
        std::printf("Could not begin the vertex upload command list.\n");
        return 1;
    }
    const BufferBarrier uploadBarriers[] = {
        {staging, ResourceState::Undefined, ResourceState::CopySource},
        {vertices, ResourceState::Undefined, ResourceState::CopyDestination},
    };
    if (!uploadList
             .Barrier(Span<const BufferBarrier>(uploadBarriers, 2),
                      Span<const cyber::rhi::TextureBarrier>{})
             .IsOk())
    {
        std::printf("Could not transition the triangle vertex buffers.\n");
        return 1;
    }
    BufferCopy vertexCopy{};
    vertexCopy.source = staging;
    vertexCopy.destination = vertices;
    vertexCopy.sizeBytes = sizeof(kTriangle);
    if (!uploadList.CopyBuffer(vertexCopy).IsOk())
    {
        std::printf("Could not copy the triangle vertex data.\n");
        return 1;
    }
    const BufferBarrier vertexReady{vertices, ResourceState::CopyDestination,
                                    ResourceState::VertexBuffer};
    if (!uploadList
             .Barrier(Span<const BufferBarrier>(&vertexReady, 1),
                      Span<const cyber::rhi::TextureBarrier>{})
             .IsOk() ||
        !uploadList.End().IsOk())
    {
        std::printf("Could not finish the triangle vertex upload.\n");
        return 1;
    }
    const CommandListHandle uploadHandle = uploadList.Handle();
    SubmitDesc uploadSubmit{};
    uploadSubmit.queue = QueueKind::Graphics;
    uploadSubmit.commandLists = Span<const CommandListHandle>(&uploadHandle, 1);
    uploadSubmit.label = "Shader hot-reload vertex upload";
    if (auto submitted = device.Submit(uploadSubmit); !submitted)
    {
        PrintFailure("Submit vertex upload", submitted.Error());
        return 1;
    }
    if (auto idle = device.WaitIdle(); !idle)
    {
        PrintFailure("WaitIdle vertex upload", idle.Error());
        return 1;
    }
    if (auto destroyed = device.DestroyBuffer(staging); !destroyed)
    {
        PrintFailure("Destroy staging buffer", destroyed.Error());
        return 1;
    }

    std::printf("Edit %s while the sample runs. Press T to toggle TINT; Escape closes.\n",
                pixelPath.CStr());
    if (selfTestEnabled)
    {
        std::printf("Running the live D3D12 shader reload self-test.\n");
    }

    SelfTest selfTest{};
    u64 frameIndex = 0;
    u32 publishedVertexVersion = 0;
    u32 publishedPixelVersion = 0;
    u64 selectedPermutation = cyber::shaders::kPermutationHashSeed;
    u64 submittedPermutation = cyber::shaders::kPermutationHashSeed;
    bool tintEnabled = false;
    bool failed = false;
    while (window.IsOpen() && !failed && selfTest.stage != SelfTestStage::Complete)
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
            else if (event.type == WindowEventType::KeyDown && event.key == Key::T)
            {
                tintEnabled = !tintEnabled;
                selectedPermutation = tintEnabled ? tintHash : cyber::shaders::kPermutationHashSeed;
                std::printf("TINT permutation %s.\n", tintEnabled ? "enabled" : "disabled");
            }
            else if (event.type == WindowEventType::Resize && event.width > 0 && event.height > 0)
            {
                const auto info = swapChain.Info();
                if (info.width != static_cast<u32>(event.width) ||
                    info.height != static_cast<u32>(event.height))
                {
                    if (auto resized = swapChain.Resize(static_cast<u32>(event.width),
                                                        static_cast<u32>(event.height));
                        !resized)
                    {
                        PrintFailure("SwapChain::Resize", resized.Error());
                        failed = true;
                        break;
                    }
                }
            }
        }
        if (failed || !window.IsOpen())
        {
            break;
        }

        library.Update();
        ShaderEvent shaderEvent{};
        while (library.PollEvent(shaderEvent))
        {
            if (shaderEvent.kind == ShaderEventKind::Failed)
            {
                const auto* error = library.LastError(shaderEvent.shader);
                if (error != nullptr)
                {
                    std::printf("%s:%u:%u: %s (previous shader retained)\n",
                                error->sourceName.Data(), error->line, error->column,
                                error->message != nullptr ? error->message : "compile failed");
                }
            }
            else
            {
                std::printf(
                    "Shader %u published in %llu ms.\n", shaderEvent.shader,
                    static_cast<unsigned long long>(shaderEvent.latencyNanoseconds / 1000000ULL));
            }
        }

        const u32 vertexVersion = library.Version(vertexId);
        const u32 pixelVersion = library.Version(pixelId);
        const bool changed = vertexVersion != publishedVertexVersion ||
                             pixelVersion != publishedPixelVersion || pipeline.Generation() == 0;
        if (vertexVersion > 0 && pixelVersion > 0 &&
            (changed || selectedPermutation != submittedPermutation))
        {
            const CompiledShader* vertex =
                library.Find(vertexId, cyber::shaders::kPermutationHashSeed);
            const CompiledShader* pixel = library.Find(pixelId, selectedPermutation);
            if (vertex != nullptr && pixel != nullptr)
            {
                if (!pipeline.Rebuild(*vertex, *pixel).IsOk())
                {
                    std::printf("Could not queue the hot pipeline rebuild.\n");
                    failed = true;
                    break;
                }
                publishedVertexVersion = vertexVersion;
                publishedPixelVersion = pixelVersion;
                submittedPermutation = selectedPermutation;
            }
        }

        pipeline.BeginFrame(frameIndex++);
        const auto info = swapChain.Info();
        if (!DrawFrame(device, swapChain, pipeline, vertices, info.width, info.height))
        {
            failed = true;
            break;
        }
        if (selfTestEnabled &&
            !UpdateSelfTest(selfTest, library, pixelId, pipeline, pixelPath, reportPath, heap))
        {
            failed = true;
        }
    }

    if (selfTestEnabled && selfTest.stage != SelfTestStage::Complete)
    {
        std::printf("The D3D12 shader hot-reload self-test did not complete.\n");
        failed = true;
    }
    if (!failed && !selfTestEnabled && !WriteReport(library, reportPath, heap))
    {
        failed = true;
    }
    return failed ? 1 : 0;
}
