#include <cyber/core/log.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/renderer/hot_pipeline.h>

#include <array>
#include <atomic>
#include <memory>
#include <utility>

namespace cyber::renderer
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::usize;
using cyber::memory::Byte;
using cyber::memory::DynamicArray;
using cyber::memory::Span;
using cyber::memory::String;

constexpr u32 kMaximumColorAttachments = 8;

// Objects one successful build owns. The layout objects are owned only when
// `ownsLayout` is set; otherwise they are shared with the current pipeline.
struct BuiltObjects
{
    rhi::ShaderHandle vertex{};
    rhi::ShaderHandle pixel{};
    rhi::PipelineHandle pipeline{};
    ReflectedLayoutObjects layoutObjects{};
    ReflectedLayout layout{};
    bool ownsLayout = false;
};

void DestroyBuilt(rhi::Device& device, BuiltObjects& objects, bool destroyLayout) noexcept
{
    if (objects.pipeline.IsValid())
    {
        static_cast<void>(device.DestroyGraphicsPipeline(objects.pipeline));
    }
    if (objects.vertex.IsValid())
    {
        static_cast<void>(device.DestroyShader(objects.vertex));
    }
    if (objects.pixel.IsValid())
    {
        static_cast<void>(device.DestroyShader(objects.pixel));
    }
    if (destroyLayout)
    {
        DestroyReflectedLayoutObjects(device, objects.layoutObjects);
    }
    objects = BuiltObjects{};
}

struct BuildJob
{
    explicit BuildJob(memory::Allocator& allocator) noexcept
        : vertexHlsl(allocator), pixelHlsl(allocator)
    {
    }

    // Inputs.
    rhi::GraphicsPipelineDesc state{};
    std::array<rhi::ColorAttachmentState, kMaximumColorAttachments> attachments{};
    u32 attachmentCount = 0;
    const char* label = nullptr;
    String vertexHlsl;
    String pixelHlsl;
    shaders::Reflection vertexReflection{};
    shaders::Reflection pixelReflection{};
    // Snapshot of the published layout, reused when the new one is equal.
    bool hasCurrentLayout = false;
    ReflectedLayout currentLayout{};
    ReflectedLayoutObjects currentObjects{};

    // CPU-only worker outputs. RHI handles are created later by the owner.
    ReflectedLayout layout{};
    ReflectedVertexLayout vertexLayout{};
    bool reuseLayout = false;
    BuiltObjects result{};
    bool ok = false;
    Error error{};
    std::atomic<bool> done{false};
};

[[nodiscard]] Span<const Byte> Bytes(const String& text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text.CStr()), text.Size());
}

void RunBuild(void* userData) noexcept
{
    BuildJob& job = *static_cast<BuildJob*>(userData);
    const auto fail = [&](const Error& error) noexcept
    {
        job.ok = false;
        job.error = error;
        job.done.store(true, std::memory_order_release);
    };

    const shaders::Reflection* stages[] = {&job.vertexReflection, &job.pixelReflection};
    auto layout = BuildReflectedLayout(Span<const shaders::Reflection* const>(stages, 2));
    if (!layout)
    {
        fail(layout.Error());
        return;
    }
    auto vertexLayout = BuildVertexLayout(job.vertexReflection);
    if (!vertexLayout)
    {
        fail(vertexLayout.Error());
        return;
    }

    job.layout = layout.Value();
    job.vertexLayout = vertexLayout.Value();
    job.reuseLayout = job.hasCurrentLayout && job.layout == job.currentLayout;
    job.ok = true;
    job.done.store(true, std::memory_order_release);
}

void FinalizeBuild(BuildJob& job, rhi::Device& device) noexcept
{
    BuiltObjects built{};
    const auto fail = [&](const Error& error) noexcept
    {
        DestroyBuilt(device, built, built.ownsLayout);
        job.ok = false;
        job.error = error;
    };

    rhi::ShaderDesc vertexDesc{};
    vertexDesc.stage = rhi::ShaderStage::Vertex;
    vertexDesc.format = rhi::ShaderBytecodeFormat::HlslSource;
    vertexDesc.code = Bytes(job.vertexHlsl);
    vertexDesc.entryPoint = "main";
    vertexDesc.label = job.label;
    auto vertex = device.CreateShader(vertexDesc);
    if (!vertex)
    {
        fail(vertex.Error());
        return;
    }
    built.vertex = vertex.Value();

    rhi::ShaderDesc pixelDesc = vertexDesc;
    pixelDesc.stage = rhi::ShaderStage::Pixel;
    pixelDesc.code = Bytes(job.pixelHlsl);
    auto pixel = device.CreateShader(pixelDesc);
    if (!pixel)
    {
        fail(pixel.Error());
        return;
    }
    built.pixel = pixel.Value();
    built.layout = job.layout;

    if (job.reuseLayout)
    {
        built.layoutObjects = job.currentObjects;
        built.ownsLayout = false;
    }
    else
    {
        auto objects = CreateReflectedLayoutObjects(device, built.layout, job.label);
        if (!objects)
        {
            fail(objects.Error());
            return;
        }
        built.layoutObjects = objects.Value();
        built.ownsLayout = true;
    }

    rhi::GraphicsPipelineDesc desc = job.state;
    desc.layout = built.layoutObjects.pipelineLayout;
    desc.vertexShader = built.vertex;
    desc.pixelShader = built.pixel;
    desc.vertexLayout = job.vertexLayout.View();
    desc.colorAttachments =
        Span<const rhi::ColorAttachmentState>(job.attachments.data(), job.attachmentCount);
    desc.label = job.label;
    auto pipeline = device.CreateGraphicsPipeline(desc);
    if (!pipeline)
    {
        fail(pipeline.Error());
        return;
    }
    built.pipeline = pipeline.Value();
    job.result = built;
}

struct Retired
{
    BuiltObjects objects{};
    bool destroyLayout = false;
    u64 retiredAt = 0;
};

}  // namespace

struct HotGraphicsPipeline::Impl
{
    Impl(const HotGraphicsPipelineDesc& source, memory::Allocator& allocator) noexcept
        : allocator(&allocator), desc(source), retired(allocator)
    {
        attachmentCount = static_cast<u32>(source.state.colorAttachments.Size());
        for (u32 index = 0; index < attachmentCount; ++index)
        {
            attachments[index] = source.state.colorAttachments[index];
        }
        desc.state.colorAttachments = Span<const rhi::ColorAttachmentState>{};
    }

    ~Impl()
    {
        rhi::Device& device = *desc.device;
        WaitForBuild();
        if (job != nullptr)
        {
            if (job->ok)
            {
                DestroyBuilt(device, job->result, job->result.ownsLayout);
            }
            DestroyJob(job);
        }
        DestroyJob(queued);
        for (usize index = 0; index < retired.Size(); ++index)
        {
            DestroyBuilt(device, retired[index].objects, retired[index].destroyLayout);
        }
        if (hasCurrent)
        {
            DestroyBuilt(device, current, true);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void DestroyJob(BuildJob*& target) noexcept
    {
        if (target != nullptr)
        {
            std::destroy_at(target);
            allocator->Deallocate(target);
            target = nullptr;
        }
    }

    void WaitForBuild() noexcept
    {
        if (job != nullptr && desc.jobs != nullptr && handle.IsValid())
        {
            desc.jobs->Wait(handle);
            handle = jobs::JobHandle{};
        }
    }

    // Takes the layout snapshot and runs or submits `queued`.
    void Start() noexcept
    {
        BuildJob* next = queued;
        queued = nullptr;
        next->hasCurrentLayout = hasCurrent;
        next->currentLayout = current.layout;
        next->currentObjects = current.layoutObjects;
        job = next;
        if (desc.jobs == nullptr)
        {
            RunBuild(job);
            return;
        }
        jobs::JobDesc jobDesc{};
        jobDesc.name = "hot pipeline build";
        jobDesc.function = &RunBuild;
        jobDesc.userData = job;
        handle = desc.jobs->Submit(jobDesc);
        if (!handle.IsValid())
        {
            // The job system is full or stopping; build here rather than lose
            // the request. This only happens under scheduler exhaustion.
            RunBuild(job);
        }
    }

    void Publish(u64 frame) noexcept
    {
        WaitForBuild();
        if (job->ok)
        {
            FinalizeBuild(*job, *desc.device);
        }
        if (job->ok)
        {
            if (hasCurrent)
            {
                Retired entry{};
                entry.objects = current;
                entry.destroyLayout = job->result.ownsLayout;
                entry.retiredAt = frame;
                if (!retired.TryPushBack(entry))
                {
                    // Without room to defer, the only safe choice is to keep the
                    // old objects alive; they are reclaimed at destruction.
                    entry.retiredAt = ~u64{0};
                }
            }
            if (!hasCurrent || job->result.ownsLayout)
            {
                ++layoutGeneration;
            }
            current = job->result;
            current.ownsLayout = true;  // whoever holds `current` owns its layout
            hasCurrent = true;
            ++generation;
            hasError = false;
        }
        else
        {
            lastError = job->error;
            hasError = true;
            CG_LOG(Error, Renderer, "hot graphics pipeline build failed: %s",
                   lastError.message != nullptr ? lastError.message : "unknown error");
        }
        DestroyJob(job);
    }

    void Collect(u64 frame) noexcept
    {
        usize kept = 0;
        for (usize index = 0; index < retired.Size(); ++index)
        {
            Retired& entry = retired[index];
            if (entry.retiredAt != ~u64{0} && frame >= entry.retiredAt + desc.framesInFlight)
            {
                DestroyBuilt(*desc.device, entry.objects, entry.destroyLayout);
                continue;
            }
            retired[kept++] = entry;
        }
        static_cast<void>(retired.TryResize(kept));
    }

    memory::Allocator* allocator = nullptr;
    HotGraphicsPipelineDesc desc{};
    std::array<rhi::ColorAttachmentState, kMaximumColorAttachments> attachments{};
    u32 attachmentCount = 0;
    BuiltObjects current{};
    bool hasCurrent = false;
    u32 generation = 0;
    u32 layoutGeneration = 0;
    BuildJob* job = nullptr;
    jobs::JobHandle handle{};
    BuildJob* queued = nullptr;
    DynamicArray<Retired> retired;
    Error lastError{};
    bool hasError = false;
};

HotGraphicsPipeline::HotGraphicsPipeline(Impl* impl) noexcept : m_impl(impl) {}

HotGraphicsPipeline::~HotGraphicsPipeline()
{
    if (m_impl != nullptr)
    {
        memory::Allocator* allocator = m_impl->allocator;
        std::destroy_at(m_impl);
        allocator->Deallocate(m_impl);
        m_impl = nullptr;
    }
}

HotGraphicsPipeline::HotGraphicsPipeline(HotGraphicsPipeline&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

HotGraphicsPipeline& HotGraphicsPipeline::operator=(HotGraphicsPipeline&& other) noexcept
{
    if (this != &other)
    {
        HotGraphicsPipeline discarded(std::move(*this));
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Result<HotGraphicsPipeline, Error>
HotGraphicsPipeline::Create(const HotGraphicsPipelineDesc& desc,
                            memory::Allocator& allocator) noexcept
{
    if (desc.device == nullptr || desc.framesInFlight == 0 ||
        desc.state.colorAttachments.Size() > kMaximumColorAttachments)
    {
        return Result<HotGraphicsPipeline, Error>::Err(
            Error{ErrorCode::InvalidArgument,
                  "hot pipeline: needs a device, framesInFlight >= 1, and <= 8 attachments"});
    }
    void* memory = allocator.Allocate(sizeof(Impl), alignof(Impl));
    if (memory == nullptr)
    {
        return Result<HotGraphicsPipeline, Error>::Err(
            Error{ErrorCode::Internal, "hot pipeline: allocation failed"});
    }
    Impl* impl = std::construct_at(static_cast<Impl*>(memory), desc, allocator);
    return Result<HotGraphicsPipeline, Error>::Ok(HotGraphicsPipeline(impl));
}

Status<Error> HotGraphicsPipeline::Rebuild(const shaders::CompiledShader& vertex,
                                           const shaders::CompiledShader& pixel) noexcept
{
    if (m_impl == nullptr || vertex.stage != shaders::Stage::Vertex ||
        pixel.stage != shaders::Stage::Pixel || vertex.hlsl.Empty() || pixel.hlsl.Empty())
    {
        return Status<Error>::Err(
            Error{ErrorCode::InvalidArgument, "hot pipeline: needs a vertex and a pixel shader"});
    }
    void* memory = m_impl->allocator->Allocate(sizeof(BuildJob), alignof(BuildJob));
    if (memory == nullptr)
    {
        return Status<Error>::Err(Error{ErrorCode::Internal, "hot pipeline: allocation failed"});
    }
    BuildJob* next = std::construct_at(static_cast<BuildJob*>(memory), *m_impl->allocator);
    next->state = m_impl->desc.state;
    next->attachments = m_impl->attachments;
    next->attachmentCount = m_impl->attachmentCount;
    next->label = m_impl->desc.label;
    next->vertexReflection = vertex.reflection;
    next->pixelReflection = pixel.reflection;
    if (!next->vertexHlsl.TryAssign(vertex.hlsl.View()) ||
        !next->pixelHlsl.TryAssign(pixel.hlsl.View()))
    {
        m_impl->DestroyJob(next);
        return Status<Error>::Err(Error{ErrorCode::Internal, "hot pipeline: allocation failed"});
    }
    m_impl->DestroyJob(m_impl->queued);  // The newest request wins.
    m_impl->queued = next;
    if (m_impl->job == nullptr)
    {
        m_impl->Start();
    }
    return Status<Error>::Ok();
}

void HotGraphicsPipeline::BeginFrame(u64 frameIndex) noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }
    if (m_impl->job != nullptr && m_impl->job->done.load(std::memory_order_acquire))
    {
        m_impl->Publish(frameIndex);
    }
    if (m_impl->job == nullptr && m_impl->queued != nullptr)
    {
        m_impl->Start();
    }
    m_impl->Collect(frameIndex);
}

rhi::PipelineHandle HotGraphicsPipeline::Current() const noexcept
{
    if (m_impl == nullptr)
    {
        return rhi::PipelineHandle{};
    }
    return m_impl->hasCurrent ? m_impl->current.pipeline : m_impl->desc.fallback;
}

bool HotGraphicsPipeline::HasBuilt() const noexcept
{
    return m_impl != nullptr && m_impl->hasCurrent;
}

rhi::BindGroupLayoutHandle
HotGraphicsPipeline::BindGroupLayout(rhi::BindGroupSlot slot) const noexcept
{
    const u32 index = static_cast<u32>(slot);
    if (m_impl == nullptr || !m_impl->hasCurrent || index >= rhi::kBindGroupSlotCount)
    {
        return rhi::BindGroupLayoutHandle{};
    }
    return m_impl->current.layoutObjects.groups[index];
}

const ReflectedLayout& HotGraphicsPipeline::Layout() const noexcept
{
    static const ReflectedLayout kEmpty{};
    return m_impl != nullptr ? m_impl->current.layout : kEmpty;
}

u32 HotGraphicsPipeline::Generation() const noexcept
{
    return m_impl != nullptr ? m_impl->generation : 0;
}

u32 HotGraphicsPipeline::LayoutGeneration() const noexcept
{
    return m_impl != nullptr ? m_impl->layoutGeneration : 0;
}

bool HotGraphicsPipeline::IsBuilding() const noexcept
{
    return m_impl != nullptr && (m_impl->job != nullptr || m_impl->queued != nullptr);
}

const Error* HotGraphicsPipeline::LastError() const noexcept
{
    return m_impl != nullptr && m_impl->hasError ? &m_impl->lastError : nullptr;
}

u32 HotGraphicsPipeline::RetiredCount() const noexcept
{
    return m_impl != nullptr ? static_cast<u32>(m_impl->retired.Size()) : 0;
}

void HotGraphicsPipeline::WaitForBuild() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->WaitForBuild();
    }
}

}  // namespace cyber::renderer
