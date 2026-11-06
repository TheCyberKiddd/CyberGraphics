#include "rhi_fixture.h"

#include <cyber/math/color.h>
#include <cyber/math/mat4.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/submission.h>
#include <cyber/scene/mesh.h>
#include <cyber/software/depth_buffer.h>
#include <cyber/software/framebuffer.h>
#include <cyber/software/image_io.h>
#include <cyber/software/pipeline.h>
#include <cyber/test/test.h>

#include <array>
#include <cstring>
#include <utility>

using cyber::core::u32;
using cyber::memory::Byte;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::rhi::BufferBarrier;
using cyber::rhi::BufferDesc;
using cyber::rhi::BufferUsage;
using cyber::rhi::CommandList;
using cyber::rhi::CommandListDesc;
using cyber::rhi::CommandListHandle;
using cyber::rhi::FenceSignal;
using cyber::rhi::QueueKind;
using cyber::rhi::ResourceState;
using cyber::rhi::SubmitDesc;
using cyber::rhi::test::Fixture;
using cyber::scene::BuildInstanceBatches;
using cyber::scene::DrawIndexedIndirectArguments;
using cyber::scene::InstanceDrawInput;
using cyber::scene::Mesh;
using cyber::scene::MeshDesc;
using cyber::scene::SimplifyMesh;
using cyber::scene::SubmeshDesc;
using cyber::scene::Vertex;

namespace
{

constexpr u32 kGridWidth = 100;
constexpr u32 kGridHeight = 100;
constexpr u32 kInstanceCount = kGridWidth * kGridHeight;
constexpr u32 kMeshColumns = 20;
constexpr u32 kMeshRows = 5;
constexpr u32 kSourceTriangleCount = kMeshColumns * kMeshRows * 2;
constexpr u32 kTargetTriangleCount = 100;
constexpr cyber::core::i32 kImageExtent = 1600;

[[nodiscard]] cyber::core::Result<Mesh, cyber::core::Error>
CreateSourceMesh(HeapAllocator& allocator) noexcept
{
    constexpr u32 kVertexCount = (kMeshColumns + 1) * (kMeshRows + 1);
    constexpr u32 kIndexCount = kSourceTriangleCount * 3;
    std::array<Vertex, kVertexCount> vertices{};
    std::array<u32, kIndexCount> indices{};

    for (u32 row = 0; row <= kMeshRows; ++row)
    {
        for (u32 column = 0; column <= kMeshColumns; ++column)
        {
            Vertex& vertex = vertices[row * (kMeshColumns + 1) + column];
            vertex.position = {static_cast<float>(column) / static_cast<float>(kMeshColumns) - 0.5f,
                               static_cast<float>(row) / static_cast<float>(kMeshRows) - 0.5f,
                               0.0f};
            const float checker = ((row + column) & 1U) == 0 ? 0.95f : 0.25f;
            vertex.color = {checker, checker, checker, 1.0f};
        }
    }

    u32 index = 0;
    for (u32 row = 0; row < kMeshRows; ++row)
    {
        for (u32 column = 0; column < kMeshColumns; ++column)
        {
            const u32 topLeft = row * (kMeshColumns + 1) + column;
            const u32 topRight = topLeft + 1;
            const u32 bottomLeft = topLeft + kMeshColumns + 1;
            const u32 bottomRight = bottomLeft + 1;
            indices[index++] = topLeft;
            indices[index++] = topRight;
            indices[index++] = bottomRight;
            indices[index++] = topLeft;
            indices[index++] = bottomRight;
            indices[index++] = bottomLeft;
        }
    }

    const SubmeshDesc submesh{0, kIndexCount, 1};
    return Mesh::Create(MeshDesc{Span<const Vertex>{vertices.data(), vertices.size()},
                                 Span<const u32>{indices.data(), indices.size()},
                                 Span<const SubmeshDesc>{&submesh, 1}},
                        allocator);
}

}  // namespace

CG_TEST(phase23_renders_one_million_instanced_triangles_with_visible_lod_transition)
{
#ifndef CYBER_PHASE23_IMAGE_PATH
#error CYBER_PHASE23_IMAGE_PATH must be set by the integration target
#endif

    HeapAllocator allocator;
    auto sourceResult = CreateSourceMesh(allocator);
    CG_REQUIRE(sourceResult);
    Mesh source = std::move(sourceResult.Value());
    CG_EXPECT_EQ(source.TriangleCount(), cyber::memory::usize{kSourceTriangleCount});

    auto lodResult = SimplifyMesh(source, kTargetTriangleCount, allocator);
    CG_REQUIRE(lodResult);
    Mesh lod = std::move(lodResult.Value().mesh);
    CG_EXPECT_TRUE(lod.TriangleCount() < source.TriangleCount());

    DynamicArray<InstanceDrawInput> inputs(allocator);
    CG_REQUIRE(inputs.TryResize(kInstanceCount));
    for (u32 index = 0; index < kInstanceCount; ++index)
    {
        const u32 column = index % kGridWidth;
        const u32 row = index / kGridWidth;
        const bool highDetail = column < kGridWidth / 2;
        const float centerX =
            -1.0f + (static_cast<float>(column) + 0.5f) * 2.0f / static_cast<float>(kGridWidth);
        const float centerY =
            1.0f - (static_cast<float>(row) + 0.5f) * 2.0f / static_cast<float>(kGridHeight);
        inputs[index] = InstanceDrawInput{
            1,
            1,
            highDetail ? 0U : 1U,
            static_cast<u32>(highDetail ? source.Indices().Size() : lod.Indices().Size()),
            0,
            0,
            cyber::math::Mat4::Translation({centerX, centerY, 0.5f})};
    }

    auto batchesResult = BuildInstanceBatches(
        Span<const InstanceDrawInput>{inputs.Data(), inputs.Size()}, allocator);
    CG_REQUIRE(batchesResult);
    const auto& batches = batchesResult.Value().Batches();
    const auto transforms = batchesResult.Value().Transforms();
    CG_REQUIRE(batches.Size() == cyber::memory::usize{2});

    static_assert(sizeof(DrawIndexedIndirectArguments) ==
                  cyber::rhi::kIndirectDrawIndexedStrideBytes);
    DynamicArray<Byte> argumentBytes(allocator);
    const cyber::memory::usize argumentBytesCount =
        batches.Size() * sizeof(DrawIndexedIndirectArguments);
    CG_REQUIRE(argumentBytes.TryResize(argumentBytesCount));
    for (cyber::memory::usize batchIndex = 0; batchIndex < batches.Size(); ++batchIndex)
    {
        std::memcpy(argumentBytes.Data() + batchIndex * sizeof(DrawIndexedIndirectArguments),
                    &batches[batchIndex].arguments, sizeof(DrawIndexedIndirectArguments));
    }

    Fixture fixture;
    CG_REQUIRE(fixture.Build());
    BufferDesc uploadDesc{};
    uploadDesc.usage = BufferUsage::HostVisible | BufferUsage::CopySource;
    uploadDesc.sizeBytes = argumentBytesCount;
    auto uploadResult = fixture.device.CreateBuffer(uploadDesc);
    CG_REQUIRE(uploadResult);
    BufferDesc indirectDesc{};
    indirectDesc.usage = BufferUsage::Indirect | BufferUsage::CopyDestination;
    indirectDesc.sizeBytes = argumentBytesCount;
    auto indirectResult = fixture.device.CreateBuffer(indirectDesc);
    CG_REQUIRE(indirectResult);
    CG_REQUIRE(fixture.device
                   .WriteBuffer(uploadResult.Value(), 0,
                                Span<const Byte>{argumentBytes.Data(), argumentBytes.Size()})
                   .IsOk());

    CommandListDesc commandListDesc{};
    commandListDesc.queue = QueueKind::Graphics;
    auto commandListResult = fixture.device.CreateCommandList(commandListDesc);
    CG_REQUIRE(commandListResult);
    CommandList commandList = std::move(commandListResult.Value());
    CG_REQUIRE(commandList.Begin().IsOk());
    const BufferBarrier initialBarriers[] = {
        {uploadResult.Value(), ResourceState::Undefined, ResourceState::CopySource},
        {indirectResult.Value(), ResourceState::Undefined, ResourceState::CopyDestination},
        {fixture.vertexBuffer, ResourceState::Undefined, ResourceState::VertexBuffer},
        {fixture.indexBuffer, ResourceState::Undefined, ResourceState::IndexBuffer},
        {fixture.uniformBuffer, ResourceState::Undefined, ResourceState::ConstantBuffer},
    };
    CG_REQUIRE(commandList.Barrier(Span<const BufferBarrier>{initialBarriers, 5}, {}).IsOk());
    CG_REQUIRE(commandList
                   .CopyBuffer(cyber::rhi::BufferCopy{
                       uploadResult.Value(), 0, indirectResult.Value(), 0, argumentBytesCount})
                   .IsOk());
    const BufferBarrier indirectBarrier{indirectResult.Value(), ResourceState::CopyDestination,
                                        ResourceState::IndirectArgument};
    CG_REQUIRE(commandList.Barrier(Span<const BufferBarrier>{&indirectBarrier, 1}, {}).IsOk());
    CG_REQUIRE(fixture.TransitionTargets(commandList));
    CG_REQUIRE(fixture.BeginPass(commandList));
    CG_REQUIRE(commandList.SetPipeline(fixture.graphicsPipeline).IsOk());
    CG_REQUIRE(commandList.SetVertexBuffer(0, fixture.vertexBuffer).IsOk());
    CG_REQUIRE(commandList.SetIndexBuffer(fixture.indexBuffer).IsOk());
    CG_REQUIRE(
        commandList.SetBindGroup(cyber::rhi::BindGroupSlot::PerMaterial, fixture.group).IsOk());
    for (cyber::memory::usize batchIndex = 0; batchIndex < batches.Size(); ++batchIndex)
    {
        CG_REQUIRE(commandList
                       .DrawIndexedIndirect(indirectResult.Value(),
                                            batchIndex * sizeof(DrawIndexedIndirectArguments), 1)
                       .IsOk());
    }
    CG_EXPECT_EQ(commandList.Stats().draws, u32{2});
    CG_REQUIRE(commandList.EndRenderPass().IsOk());
    CG_REQUIRE(commandList.End().IsOk());
    const CommandListHandle commandListHandle = commandList.Handle();
    const FenceSignal completion{fixture.fence, 1};
    SubmitDesc submission{};
    submission.queue = QueueKind::Graphics;
    submission.commandLists = Span<const CommandListHandle>{&commandListHandle, 1};
    submission.signals = Span<const FenceSignal>{&completion, 1};
    CG_REQUIRE(fixture.device.Submit(submission).IsOk());
    CG_REQUIRE(fixture.device.WaitFence(fixture.fence, 1).IsOk());

    cyber::memory::usize renderedTriangleCount = 0;
    for (cyber::memory::usize batchIndex = 0; batchIndex < batches.Size(); ++batchIndex)
    {
        const auto& batch = batches[batchIndex];
        renderedTriangleCount += static_cast<cyber::memory::usize>(batch.arguments.indexCount / 3) *
                                 batch.arguments.instanceCount;
    }
    CG_EXPECT_TRUE(renderedTriangleCount >= cyber::memory::usize{1000000});

    DynamicArray<cyber::software::Vertex> renderVertices(allocator);
    CG_REQUIRE(renderVertices.TryResize(renderedTriangleCount * 3));
    cyber::memory::usize output = 0;
    for (cyber::memory::usize batchIndex = 0; batchIndex < batches.Size(); ++batchIndex)
    {
        const auto& batch = batches[batchIndex];
        const Mesh& mesh = batch.lod == 0 ? source : lod;
        const auto indices = mesh.Indices();
        const auto vertices = mesh.Vertices();
        const cyber::math::Color lodTint = batch.lod == 0
                                               ? cyber::math::Color{1.0f, 0.35f, 0.12f, 1.0f}
                                               : cyber::math::Color{0.12f, 0.55f, 1.0f, 1.0f};

        for (u32 instance = 0; instance < batch.arguments.instanceCount; ++instance)
        {
            const cyber::math::Mat4& transform =
                transforms[batch.arguments.firstInstance + instance];
            for (u32 index = 0; index < batch.arguments.indexCount; ++index)
            {
                const u32 vertexIndex = indices[batch.arguments.firstIndex + index] +
                                        static_cast<u32>(batch.arguments.vertexOffset);
                const Vertex& sourceVertex = vertices[vertexIndex];
                const cyber::math::Color vertexColor{sourceVertex.color.x * lodTint.r,
                                                     sourceVertex.color.y * lodTint.g,
                                                     sourceVertex.color.z * lodTint.b, 1.0f};
                const float scale = 1.8f / static_cast<float>(kGridWidth);
                renderVertices[output++] = cyber::software::Vertex{
                    {transform[3].x + sourceVertex.position.x * scale,
                     transform[3].y + sourceVertex.position.y * scale, transform[3].z},
                    vertexColor};
            }
        }
    }
    CG_EXPECT_EQ(output, renderVertices.Size());

    cyber::software::FramebufferDesc framebufferDesc{};
    framebufferDesc.width = kImageExtent;
    framebufferDesc.height = kImageExtent;
    auto framebufferResult = cyber::software::Framebuffer::Create(framebufferDesc, allocator);
    CG_REQUIRE(framebufferResult);
    auto framebuffer = std::move(framebufferResult.Value());
    framebuffer.Clear({0.015f, 0.02f, 0.03f, 1.0f});

    cyber::software::DepthBufferDesc depthDesc{};
    depthDesc.width = kImageExtent;
    depthDesc.height = kImageExtent;
    auto depthResult = cyber::software::DepthBuffer::Create(depthDesc, allocator);
    CG_REQUIRE(depthResult);
    auto depth = std::move(depthResult.Value());
    depth.Clear();

    cyber::software::PipelineDesc pipeline{};
    pipeline.viewport = {0.0f, 0.0f, static_cast<float>(kImageExtent),
                         static_cast<float>(kImageExtent)};
    pipeline.cullMode = cyber::software::CullMode::None;
    cyber::software::PipelineStats stats{};
    const auto rendered = cyber::software::DrawTriangles(
        pipeline, Span<const cyber::software::Vertex>{renderVertices.Data(), renderVertices.Size()},
        framebuffer, depth, nullptr, allocator, &stats);
    CG_REQUIRE(rendered);
    CG_EXPECT_EQ(stats.triangles, static_cast<cyber::core::u64>(renderedTriangleCount));
    CG_EXPECT_TRUE(stats.triangles >= cyber::core::u64{1000000});
    CG_EXPECT_TRUE(stats.trianglesSubmitted > 0);
    CG_EXPECT_TRUE(stats.pixels > 0);

    const auto highDetailPixel =
        framebuffer.GetPixel(kImageExtent / 4 - kImageExtent / 200, kImageExtent / 200);
    const auto lowDetailPixel =
        framebuffer.GetPixel((kImageExtent * 3) / 4 - kImageExtent / 200, kImageExtent / 200);
    CG_EXPECT_TRUE(highDetailPixel.r > highDetailPixel.b);
    CG_EXPECT_TRUE(lowDetailPixel.b > lowDetailPixel.r);

    const auto written = cyber::software::WriteImage(
        CYBER_PHASE23_IMAGE_PATH, framebuffer, allocator,
        cyber::software::ImageWriteDesc{cyber::software::ImageFormat::Ppm, true});
    CG_EXPECT_TRUE(written);
}
