#include <cyber/jobs/job_system.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/renderer/hot_pipeline.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/shaders/compiler.h>
#include <cyber/test/test.h>

#include <utility>

using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::jobs::JobSystem;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;
using cyber::renderer::HotGraphicsPipeline;
using cyber::renderer::HotGraphicsPipelineDesc;
using cyber::rhi::BindGroupSlot;
using cyber::rhi::ColorAttachmentState;
using cyber::rhi::Device;
using cyber::rhi::DeviceDesc;
using cyber::rhi::Format;
using cyber::rhi::PipelineHandle;
using cyber::shaders::CompiledShader;
using cyber::shaders::CompileHlsl;

namespace
{

constexpr char kVertex[] =
    "shader vertex Cube {\n"
    "    input float3 position @location(0);\n"
    "    input float2 uv @location(1);\n"
    "    output float4 clipPosition @builtin(position);\n"
    "    output float2 texcoord @location(0);\n"
    "    uniform float4x4 modelViewProjection @register(b0);\n"
    "    output.clipPosition = mul(modelViewProjection, float4(input.position, 1.0));\n"
    "    output.texcoord = input.uv;\n"
    "}\n";

constexpr char kPixelTextured[] = "shader pixel Cube {\n"
                                  "    input float4 position @builtin(position);\n"
                                  "    input float2 uv @location(0);\n"
                                  "    texture2d albedo @register(t0);\n"
                                  "    sampler albedoSampler @register(s0);\n"
                                  "    return sample(albedo, albedoSampler, input.uv);\n"
                                  "}\n";

// Same bindings, different code: a reload that keeps the layout.
constexpr char kPixelTexturedSwizzled[] =
    "shader pixel Cube {\n"
    "    input float4 position @builtin(position);\n"
    "    input float2 uv @location(0);\n"
    "    texture2d albedo @register(t0);\n"
    "    sampler albedoSampler @register(s0);\n"
    "    return mul(sample(albedo, albedoSampler, input.uv), float4(1.0, 0.5, 0.5, 1.0));\n"
    "}\n";

// Different bindings: a reload that changes the layout.
constexpr char kPixelSolid[] = "shader pixel Cube {\n"
                               "    input float4 position @builtin(position);\n"
                               "    input float2 uv @location(0);\n"
                               "    return float4(input.uv, 0.0, 1.0);\n"
                               "}\n";

// Valid shader language whose bindings the RHI layout cannot express.
constexpr char kPixelUnbindable[] = "shader pixel Cube {\n"
                                    "    input float2 uv @location(0);\n"
                                    "    uniform float4 tint @register(b3);\n"
                                    "    return tint;\n"
                                    "}\n";

struct Fixture
{
    HeapAllocator heap;
    Device device;
    ColorAttachmentState attachment{};

    [[nodiscard]] bool Open()
    {
        DeviceDesc desc{};
        desc.backend = cyber::rhi::NullBackend();
        auto created = Device::Create(desc, heap);
        if (!created)
        {
            return false;
        }
        device = std::move(created.Value());
        attachment.format = Format::RGBA8Unorm;
        return true;
    }

    [[nodiscard]] HotGraphicsPipelineDesc Desc(JobSystem* jobs, PipelineHandle fallback)
    {
        HotGraphicsPipelineDesc desc{};
        desc.device = &device;
        desc.jobs = jobs;
        desc.fallback = fallback;
        desc.framesInFlight = 2;
        desc.state.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);
        desc.state.depthStencil.depthTestEnabled = false;
        desc.state.depthStencil.depthWriteEnabled = false;
        return desc;
    }
};

[[nodiscard]] CompiledShader Compile(const char* source, HeapAllocator& heap)
{
    auto compiled = CompileHlsl("test.cgs", StringView(source), heap);
    CG_ASSERT(static_cast<bool>(compiled));
    return std::move(compiled.Value());
}

}  // namespace

CG_TEST(renderer_hot_pipeline_validates_creation)
{
    HeapAllocator heap;
    HotGraphicsPipelineDesc desc{};
    CG_EXPECT_FALSE(static_cast<bool>(HotGraphicsPipeline::Create(desc, heap)));
    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    desc = fixture.Desc(nullptr, PipelineHandle{});
    desc.framesInFlight = 0;
    CG_EXPECT_FALSE(static_cast<bool>(HotGraphicsPipeline::Create(desc, heap)));

    desc.framesInFlight = 1;
    auto created = HotGraphicsPipeline::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    HotGraphicsPipeline pipeline = std::move(created.Value());
    CompiledShader vertex = Compile(kVertex, heap);
    CompiledShader pixel = Compile(kPixelTextured, heap);
    // Stages swapped are rejected.
    auto swapped = pipeline.Rebuild(pixel, vertex);
    CG_REQUIRE(!swapped);
    CG_EXPECT_EQ(swapped.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(renderer_hot_pipeline_swaps_only_at_frame_boundaries)
{
    const u32 workerCounts[] = {0, 1, 4};
    for (const u32 workers : workerCounts)
    {
        Fixture fixture;
        CG_REQUIRE(fixture.Open());
        {
            JobSystem jobs;
            if (workers != 0)
            {
                JobSystem::Desc jobsDesc{};
                jobsDesc.workerCount = workers;
                auto created = JobSystem::Create(jobsDesc, fixture.heap);
                CG_REQUIRE(static_cast<bool>(created));
                jobs = std::move(created.Value());
            }
            JobSystem* jobPointer = workers != 0 ? &jobs : nullptr;

            // A real pipeline stands in for a renderer's fallback.
            HeapAllocator scratch;
            auto fallbackBuild =
                HotGraphicsPipeline::Create(fixture.Desc(nullptr, PipelineHandle{}), fixture.heap);
            CG_REQUIRE(static_cast<bool>(fallbackBuild));
            HotGraphicsPipeline fallbackOwner = std::move(fallbackBuild.Value());
            CG_REQUIRE(
                fallbackOwner.Rebuild(Compile(kVertex, scratch), Compile(kPixelSolid, scratch))
                    .IsOk());
            fallbackOwner.BeginFrame(0);
            const PipelineHandle fallback = fallbackOwner.Current();
            CG_REQUIRE(fallback.IsValid());

            auto created =
                HotGraphicsPipeline::Create(fixture.Desc(jobPointer, fallback), fixture.heap);
            CG_REQUIRE(static_cast<bool>(created));
            HotGraphicsPipeline pipeline = std::move(created.Value());
            CG_EXPECT_TRUE(pipeline.Current() == fallback);
            CG_EXPECT_FALSE(pipeline.HasBuilt());
            CG_EXPECT_EQ(pipeline.LayoutGeneration(), u32{0});

            CompiledShader vertex = Compile(kVertex, scratch);
            CG_REQUIRE(pipeline.Rebuild(vertex, Compile(kPixelTextured, scratch)).IsOk());
            CG_EXPECT_TRUE(pipeline.IsBuilding());
            // Until a frame boundary, the fallback stays current even if the
            // build has finished.
            pipeline.WaitForBuild();
            CG_EXPECT_TRUE(pipeline.Current() == fallback);

            u64 frame = 1;
            pipeline.BeginFrame(frame++);
            CG_REQUIRE(pipeline.HasBuilt());
            const PipelineHandle first = pipeline.Current();
            CG_EXPECT_FALSE(first == fallback);
            CG_EXPECT_EQ(pipeline.Generation(), u32{1});
            CG_EXPECT_EQ(pipeline.LayoutGeneration(), u32{1});
            const auto group = pipeline.BindGroupLayout(BindGroupSlot::PerFrame);
            CG_EXPECT_TRUE(group.IsValid());
            CG_EXPECT_EQ(pipeline.Layout().pushConstants.sizeBytes, u32{64});

            // Same layout: new pipeline, same bind-group layout handle.
            CG_REQUIRE(pipeline.Rebuild(vertex, Compile(kPixelTexturedSwizzled, scratch)).IsOk());
            pipeline.WaitForBuild();
            pipeline.BeginFrame(frame++);
            CG_EXPECT_EQ(pipeline.Generation(), u32{2});
            CG_EXPECT_FALSE(pipeline.Current() == first);
            CG_EXPECT_EQ(pipeline.LayoutGeneration(), u32{1});
            CG_EXPECT_TRUE(pipeline.BindGroupLayout(BindGroupSlot::PerFrame) == group);
            CG_EXPECT_EQ(pipeline.RetiredCount(), u32{1});

            // A build the layout cannot express keeps the previous pipeline.
            const PipelineHandle second = pipeline.Current();
            CG_REQUIRE(pipeline.Rebuild(vertex, Compile(kPixelUnbindable, scratch)).IsOk());
            pipeline.WaitForBuild();
            pipeline.BeginFrame(frame++);
            CG_EXPECT_TRUE(pipeline.Current() == second);
            CG_REQUIRE(pipeline.LastError() != nullptr);
            CG_EXPECT_EQ(pipeline.LastError()->code, ErrorCode::Unsupported);
            CG_EXPECT_EQ(pipeline.Generation(), u32{2});

            // A layout change gives new bind-group layouts.
            CG_REQUIRE(pipeline.Rebuild(vertex, Compile(kPixelSolid, scratch)).IsOk());
            pipeline.WaitForBuild();
            pipeline.BeginFrame(frame++);
            CG_EXPECT_TRUE(pipeline.LastError() == nullptr);
            CG_EXPECT_EQ(pipeline.LayoutGeneration(), u32{2});
            CG_EXPECT_FALSE(pipeline.BindGroupLayout(BindGroupSlot::PerFrame).IsValid());

            // Retired objects outlive the frames-in-flight window, then go.
            CG_EXPECT_TRUE(pipeline.RetiredCount() >= 1);
            pipeline.BeginFrame(frame + 4);
            CG_EXPECT_EQ(pipeline.RetiredCount(), u32{0});
        }
        CG_EXPECT_EQ(fixture.device.ReportLeaks(), u32{0});
    }
}

CG_TEST(renderer_hot_pipeline_newest_queued_request_wins)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    {
        JobSystem::Desc jobsDesc{};
        jobsDesc.workerCount = 2;
        auto jobs = JobSystem::Create(jobsDesc, fixture.heap);
        CG_REQUIRE(static_cast<bool>(jobs));
        HeapAllocator scratch;
        auto created = HotGraphicsPipeline::Create(fixture.Desc(&jobs.Value(), PipelineHandle{}),
                                                   fixture.heap);
        CG_REQUIRE(static_cast<bool>(created));
        HotGraphicsPipeline pipeline = std::move(created.Value());
        CompiledShader vertex = Compile(kVertex, scratch);
        CG_REQUIRE(pipeline.Rebuild(vertex, Compile(kPixelTextured, scratch)).IsOk());
        CG_REQUIRE(pipeline.Rebuild(vertex, Compile(kPixelTexturedSwizzled, scratch)).IsOk());
        CG_REQUIRE(pipeline.Rebuild(vertex, Compile(kPixelSolid, scratch)).IsOk());
        for (u64 frame = 0; frame < 16 && pipeline.IsBuilding(); ++frame)
        {
            pipeline.WaitForBuild();
            pipeline.BeginFrame(frame);
        }
        CG_EXPECT_FALSE(pipeline.IsBuilding());
        // The first request was running; the second was replaced by the third.
        CG_EXPECT_EQ(pipeline.Generation(), u32{2});
        CG_EXPECT_EQ(pipeline.Layout().bindingCounts[0], u32{0});
    }
    CG_EXPECT_EQ(fixture.device.ReportLeaks(), u32{0});
}
