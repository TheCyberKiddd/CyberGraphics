#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/renderer/render_graph.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/test/test.h>

#include <cstring>
#include <fstream>
#include <utility>

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Status;
using cyber::core::u32;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::renderer::GraphResourceHandle;
using cyber::renderer::RenderGraph;
using cyber::renderer::RenderPassBuilder;
using cyber::renderer::RenderPassContext;
using cyber::renderer::RenderPassDesc;
using cyber::rhi::CommandList;
using cyber::rhi::CommandListDesc;
using cyber::rhi::Device;
using cyber::rhi::DeviceDesc;
using cyber::rhi::Format;
using cyber::rhi::QueueKind;
using cyber::rhi::ResourceState;
using cyber::rhi::SubmitDesc;
using cyber::rhi::TextureDesc;
using cyber::rhi::TextureUsage;

namespace
{

struct PassData
{
    GraphResourceHandle reads[2]{};
    ResourceState readStates[2]{};
    u32 readCount = 0;
    GraphResourceHandle writes[2]{};
    ResourceState writeStates[2]{};
    u32 writeCount = 0;
    u32 executeCount = 0;
    bool resolved = true;
    bool debugLabel = false;
    cyber::rhi::TextureHandle firstTexture{};
    cyber::rhi::BufferHandle firstBuffer{};
};

[[nodiscard]] Status<Error> SetupPass(RenderPassBuilder& builder, void* userData) noexcept
{
    auto& data = *static_cast<PassData*>(userData);
    for (u32 index = 0; index < data.readCount; ++index)
    {
        const Status<Error> result = builder.Read(data.reads[index], data.readStates[index]);
        if (!result)
        {
            return result;
        }
    }
    for (u32 index = 0; index < data.writeCount; ++index)
    {
        const Status<Error> result = builder.Write(data.writes[index], data.writeStates[index]);
        if (!result)
        {
            return result;
        }
    }
    return Status<Error>::Ok();
}

[[nodiscard]] Status<Error> ExecutePass(RenderPassContext& context, void* userData) noexcept
{
    auto& data = *static_cast<PassData*>(userData);
    ++data.executeCount;
    for (u32 index = 0; index < data.readCount; ++index)
    {
        const GraphResourceHandle resource = data.reads[index];
        const auto texture = context.GetTexture(resource);
        const auto buffer = context.GetBuffer(resource);
        data.resolved = data.resolved && (texture.IsValid() || buffer.IsValid());
        if (texture.IsValid() && !data.firstTexture.IsValid())
        {
            data.firstTexture = texture;
        }
        if (buffer.IsValid() && !data.firstBuffer.IsValid())
        {
            data.firstBuffer = buffer;
        }
    }
    for (u32 index = 0; index < data.writeCount; ++index)
    {
        const GraphResourceHandle resource = data.writes[index];
        const auto texture = context.GetTexture(resource);
        const auto buffer = context.GetBuffer(resource);
        data.resolved = data.resolved && (texture.IsValid() || buffer.IsValid());
        if (texture.IsValid() && !data.firstTexture.IsValid())
        {
            data.firstTexture = texture;
        }
        if (buffer.IsValid() && !data.firstBuffer.IsValid())
        {
            data.firstBuffer = buffer;
        }
    }
    if (data.debugLabel)
    {
        const Status<Error> begun = context.Commands().BeginDebugLabel("phase 20 graph pass");
        if (!begun)
        {
            return begun;
        }
        return context.Commands().EndDebugLabel();
    }
    return Status<Error>::Ok();
}

[[nodiscard]] RenderPassDesc MakePass(const char* name, PassData& data,
                                      bool sideEffect = false) noexcept
{
    RenderPassDesc desc{};
    desc.name = name;
    desc.setup = &SetupPass;
    desc.execute = &ExecutePass;
    desc.userData = &data;
    desc.sideEffect = sideEffect;
    return desc;
}

struct Fixture
{
    HeapAllocator heap;
    Device device;

    [[nodiscard]] bool Open() noexcept
    {
        DeviceDesc desc{};
        desc.backend = cyber::rhi::NullBackend();
        auto created = Device::Create(desc, heap);
        if (!created)
        {
            return false;
        }
        device = std::move(created.Value());
        return true;
    }

    [[nodiscard]] bool CreateRecordingList(CommandList& list) noexcept
    {
        CommandListDesc desc{};
        desc.queue = QueueKind::Graphics;
        auto created = device.CreateCommandList(desc);
        if (!created)
        {
            return false;
        }
        list = std::move(created.Value());
        return list.Begin().IsOk();
    }
};

}  // namespace

CG_TEST(renderer_render_graph_builds_and_records_multipass_frame)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());

    TextureDesc outputDesc{};
    outputDesc.format = Format::RGBA8Unorm;
    outputDesc.usage = TextureUsage::RenderTarget;
    outputDesc.width = 64;
    outputDesc.height = 64;
    auto outputCreated = fixture.device.CreateTexture(outputDesc);
    CG_REQUIRE(static_cast<bool>(outputCreated));

    RenderGraph graph(fixture.device, fixture.heap);
    auto output = graph.ImportTexture("backbuffer", outputCreated.Value());
    CG_REQUIRE(static_cast<bool>(output));

    TextureDesc depthDesc{};
    depthDesc.format = Format::D32Float;
    depthDesc.usage = TextureUsage::DepthStencil;
    depthDesc.width = 64;
    depthDesc.height = 64;
    auto depth = graph.CreateTransientTexture("depth", depthDesc);
    CG_REQUIRE(static_cast<bool>(depth));

    TextureDesc colorDesc{};
    colorDesc.format = Format::RGBA8Unorm;
    colorDesc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
    colorDesc.width = 64;
    colorDesc.height = 64;
    auto sceneColor = graph.CreateTransientTexture("scene_color", colorDesc);
    CG_REQUIRE(static_cast<bool>(sceneColor));

    TextureDesc unusedDesc{};
    unusedDesc.format = Format::RGBA8Unorm;
    unusedDesc.usage = TextureUsage::RenderTarget;
    unusedDesc.width = 16;
    unusedDesc.height = 16;
    auto unused = graph.CreateTransientTexture("unused", unusedDesc);
    CG_REQUIRE(static_cast<bool>(unused));

    PassData depthData{};
    depthData.writes[0] = depth.Value();
    depthData.writeStates[0] = ResourceState::DepthWrite;
    depthData.writeCount = 1;
    depthData.debugLabel = true;
    CG_REQUIRE(graph.AddPass(MakePass("Depth prepass", depthData)).IsOk());

    PassData mainData{};
    mainData.reads[0] = depth.Value();
    mainData.readStates[0] = ResourceState::DepthRead;
    mainData.readCount = 1;
    mainData.writes[0] = sceneColor.Value();
    mainData.writeStates[0] = ResourceState::RenderTarget;
    mainData.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("Main pass", mainData)).IsOk());

    PassData postData{};
    postData.reads[0] = sceneColor.Value();
    postData.readStates[0] = ResourceState::ShaderRead;
    postData.readCount = 1;
    postData.writes[0] = output.Value();
    postData.writeStates[0] = ResourceState::RenderTarget;
    postData.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("Post-processing", postData)).IsOk());

    PassData unusedData{};
    unusedData.writes[0] = unused.Value();
    unusedData.writeStates[0] = ResourceState::RenderTarget;
    unusedData.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("Unused branch", unusedData)).IsOk());
    CG_REQUIRE(graph.Export(output.Value(), ResourceState::Present).IsOk());
    CG_REQUIRE(graph.Compile().IsOk());
    CG_EXPECT_EQ(graph.PassCount(), u32{4});
    CG_EXPECT_EQ(graph.CulledPassCount(), u32{1});
    CG_EXPECT_EQ(graph.TransientAllocationCount(), u32{2});

    char text[4096]{};
    auto textSize = graph.WriteTextDump(Span<char>(text));
    CG_REQUIRE(static_cast<bool>(textSize));
    CG_EXPECT_TRUE(std::strstr(text, "Post-processing") != nullptr);
    CG_EXPECT_TRUE(std::strstr(text, "Unused branch culled") != nullptr);

    char tooSmall[8];
    std::memset(tooSmall, 'X', sizeof(tooSmall));
    auto smallDot = graph.WriteGraphvizDot(Span<char>(tooSmall));
    CG_EXPECT_FALSE(static_cast<bool>(smallDot));
    CG_EXPECT_EQ(tooSmall[0], 'X');
    auto smallText = graph.WriteTextDump(Span<char>(tooSmall));
    CG_EXPECT_FALSE(static_cast<bool>(smallText));
    CG_EXPECT_EQ(tooSmall[0], 'X');

    char dot[4096]{};
    auto dotSize = graph.WriteGraphvizDot(Span<char>(dot));
    CG_REQUIRE(static_cast<bool>(dotSize));
    CG_EXPECT_TRUE(std::strstr(dot, "digraph RenderGraph") != nullptr);
    std::ofstream dump("phase20_render_graph.dot", std::ios::binary);
    CG_REQUIRE(dump.good());
    dump.write(dot, static_cast<std::streamsize>(dotSize.Value()));
    CG_REQUIRE(dump.good());
    dump.close();

    CommandList commandList;
    CG_REQUIRE(fixture.CreateRecordingList(commandList));
    CG_REQUIRE(graph.Execute(commandList).IsOk());
    CG_EXPECT_EQ(depthData.executeCount, u32{1});
    CG_EXPECT_EQ(mainData.executeCount, u32{1});
    CG_EXPECT_EQ(postData.executeCount, u32{1});
    CG_EXPECT_EQ(unusedData.executeCount, u32{0});
    CG_EXPECT_TRUE(depthData.resolved);
    CG_EXPECT_TRUE(mainData.resolved);
    CG_EXPECT_TRUE(postData.resolved);
    CG_REQUIRE(commandList.End().IsOk());
    const cyber::rhi::CommandListHandle listHandle = commandList.Handle();
    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const cyber::rhi::CommandListHandle>(&listHandle, 1);
    CG_REQUIRE(fixture.device.Submit(submit).IsOk());
    CG_REQUIRE(graph.AcknowledgeSubmission().IsOk());
    CG_EXPECT_EQ(fixture.device.TextureStateOf(outputCreated.Value(), 0, 0),
                 ResourceState::Present);
    CG_EXPECT_EQ(fixture.device.TextureStateOf(depthData.firstTexture, 0, 0),
                 ResourceState::DepthRead);
    CG_EXPECT_EQ(fixture.device.TextureStateOf(postData.firstTexture, 0, 0),
                 ResourceState::ShaderRead);

    CommandList secondCommandList;
    CG_REQUIRE(fixture.CreateRecordingList(secondCommandList));
    CG_REQUIRE(graph.Execute(secondCommandList).IsOk());
    CG_REQUIRE(secondCommandList.End().IsOk());
    const cyber::rhi::CommandListHandle secondListHandle = secondCommandList.Handle();
    submit.commandLists = Span<const cyber::rhi::CommandListHandle>(&secondListHandle, 1);
    CG_REQUIRE(fixture.device.Submit(submit).IsOk());
    CG_REQUIRE(graph.AcknowledgeSubmission().IsOk());
    CG_EXPECT_EQ(depthData.executeCount, u32{2});
    CG_EXPECT_EQ(mainData.executeCount, u32{2});
    CG_EXPECT_EQ(postData.executeCount, u32{2});
    CG_REQUIRE(graph.Destroy().IsOk());
    CG_REQUIRE(fixture.device.DestroyTexture(outputCreated.Value()).IsOk());
}

CG_TEST(renderer_render_graph_aliases_only_disjoint_compatible_transients)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    RenderGraph graph(fixture.device, fixture.heap);

    TextureDesc desc{};
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::RenderTarget;
    desc.width = 32;
    desc.height = 32;
    auto first = graph.CreateTransientTexture("first transient", desc);
    auto second = graph.CreateTransientTexture("second transient", desc);
    CG_REQUIRE(static_cast<bool>(first));
    CG_REQUIRE(static_cast<bool>(second));

    PassData firstData{};
    firstData.writes[0] = first.Value();
    firstData.writeStates[0] = ResourceState::RenderTarget;
    firstData.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("first side effect", firstData, true)).IsOk());
    PassData secondData{};
    secondData.writes[0] = second.Value();
    secondData.writeStates[0] = ResourceState::RenderTarget;
    secondData.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("second side effect", secondData, true)).IsOk());
    CG_REQUIRE(graph.Compile().IsOk());
    CG_EXPECT_EQ(graph.TransientAllocationCount(), u32{1});
    CG_EXPECT_EQ(graph.AliasedResourceCount(), u32{1});

    CommandList commandList;
    CG_REQUIRE(fixture.CreateRecordingList(commandList));
    CG_REQUIRE(graph.Execute(commandList).IsOk());
    CG_EXPECT_TRUE(firstData.firstTexture.IsValid());
    CG_EXPECT_EQ(firstData.firstTexture, secondData.firstTexture);
    CG_REQUIRE(commandList.End().IsOk());
    commandList.Reset();
    CG_REQUIRE(graph.Destroy().IsOk());
}

CG_TEST(renderer_render_graph_rejects_transient_read_before_write)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    RenderGraph graph(fixture.device, fixture.heap);

    TextureDesc desc{};
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::Sampled;
    desc.width = 8;
    desc.height = 8;
    auto transient = graph.CreateTransientTexture("uninitialized", desc);
    CG_REQUIRE(static_cast<bool>(transient));

    PassData reader{};
    reader.reads[0] = transient.Value();
    reader.readStates[0] = ResourceState::ShaderRead;
    reader.readCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("invalid reader", reader, true)).IsOk());
    const Status<Error> compiled = graph.Compile();
    CG_EXPECT_FALSE(compiled.IsOk());
    CG_EXPECT_EQ(compiled.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(renderer_render_graph_preserves_diamond_dependencies)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());

    TextureDesc outputDesc{};
    outputDesc.format = Format::RGBA8Unorm;
    outputDesc.usage = TextureUsage::RenderTarget;
    outputDesc.width = 16;
    outputDesc.height = 16;
    auto outputCreated = fixture.device.CreateTexture(outputDesc);
    CG_REQUIRE(static_cast<bool>(outputCreated));
    RenderGraph graph(fixture.device, fixture.heap);
    auto output = graph.ImportTexture("diamond output", outputCreated.Value());
    CG_REQUIRE(static_cast<bool>(output));

    TextureDesc transientDesc{};
    transientDesc.format = Format::RGBA8Unorm;
    transientDesc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
    transientDesc.width = 16;
    transientDesc.height = 16;
    auto root = graph.CreateTransientTexture("root", transientDesc);
    auto left = graph.CreateTransientTexture("left", transientDesc);
    auto right = graph.CreateTransientTexture("right", transientDesc);
    CG_REQUIRE(static_cast<bool>(root));
    CG_REQUIRE(static_cast<bool>(left));
    CG_REQUIRE(static_cast<bool>(right));

    PassData rootData{};
    rootData.writes[0] = root.Value();
    rootData.writeStates[0] = ResourceState::RenderTarget;
    rootData.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("root pass", rootData)).IsOk());

    PassData leftData{};
    leftData.reads[0] = root.Value();
    leftData.readStates[0] = ResourceState::ShaderRead;
    leftData.readCount = 1;
    leftData.writes[0] = left.Value();
    leftData.writeStates[0] = ResourceState::RenderTarget;
    leftData.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("left pass", leftData)).IsOk());

    PassData rightData{};
    rightData.reads[0] = root.Value();
    rightData.readStates[0] = ResourceState::ShaderRead;
    rightData.readCount = 1;
    rightData.writes[0] = right.Value();
    rightData.writeStates[0] = ResourceState::RenderTarget;
    rightData.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("right pass", rightData)).IsOk());

    PassData joinData{};
    joinData.reads[0] = left.Value();
    joinData.readStates[0] = ResourceState::ShaderRead;
    joinData.reads[1] = right.Value();
    joinData.readStates[1] = ResourceState::ShaderRead;
    joinData.readCount = 2;
    joinData.writes[0] = output.Value();
    joinData.writeStates[0] = ResourceState::RenderTarget;
    joinData.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("join pass", joinData)).IsOk());
    CG_REQUIRE(graph.Export(output.Value(), ResourceState::Present).IsOk());
    CG_REQUIRE(graph.Compile().IsOk());
    CG_EXPECT_EQ(graph.CulledPassCount(), u32{0});

    char text[4096]{};
    auto dump = graph.WriteTextDump(Span<char>(text));
    CG_REQUIRE(static_cast<bool>(dump));
    CG_EXPECT_TRUE(std::strstr(text, "P0 -> P1") != nullptr);
    CG_EXPECT_TRUE(std::strstr(text, "P0 -> P2") != nullptr);
    CG_EXPECT_TRUE(std::strstr(text, "P1 -> P3") != nullptr);
    CG_EXPECT_TRUE(std::strstr(text, "P2 -> P3") != nullptr);

    CommandList commandList;
    CG_REQUIRE(fixture.CreateRecordingList(commandList));
    CG_REQUIRE(graph.Execute(commandList).IsOk());
    CG_REQUIRE(commandList.End().IsOk());
    const cyber::rhi::CommandListHandle listHandle = commandList.Handle();
    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const cyber::rhi::CommandListHandle>(&listHandle, 1);
    CG_REQUIRE(fixture.device.Submit(submit).IsOk());
    CG_REQUIRE(graph.AcknowledgeSubmission().IsOk());
    CG_REQUIRE(graph.Destroy().IsOk());
    CG_REQUIRE(fixture.device.DestroyTexture(outputCreated.Value()).IsOk());
}

CG_TEST(renderer_render_graph_keeps_overlapping_transient_lifetimes_separate)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    RenderGraph graph(fixture.device, fixture.heap);

    TextureDesc desc{};
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
    desc.width = 16;
    desc.height = 16;
    auto first = graph.CreateTransientTexture("overlap first", desc);
    auto second = graph.CreateTransientTexture("overlap second", desc);
    CG_REQUIRE(static_cast<bool>(first));
    CG_REQUIRE(static_cast<bool>(second));

    PassData firstWrite{};
    firstWrite.writes[0] = first.Value();
    firstWrite.writeStates[0] = ResourceState::RenderTarget;
    firstWrite.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("write first", firstWrite)).IsOk());
    PassData secondWrite{};
    secondWrite.writes[0] = second.Value();
    secondWrite.writeStates[0] = ResourceState::RenderTarget;
    secondWrite.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("write second", secondWrite)).IsOk());
    PassData join{};
    join.reads[0] = first.Value();
    join.readStates[0] = ResourceState::ShaderRead;
    join.reads[1] = second.Value();
    join.readStates[1] = ResourceState::ShaderRead;
    join.readCount = 2;
    CG_REQUIRE(graph.AddPass(MakePass("read both", join, true)).IsOk());

    CG_REQUIRE(graph.Compile().IsOk());
    CG_EXPECT_EQ(graph.TransientAllocationCount(), u32{2});
    CG_EXPECT_EQ(graph.AliasedResourceCount(), u32{0});
}

CG_TEST(renderer_render_graph_transitions_transient_buffers)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    RenderGraph graph(fixture.device, fixture.heap);

    cyber::rhi::BufferDesc desc{};
    desc.usage = cyber::rhi::BufferUsage::Storage;
    desc.sizeBytes = 256;
    auto buffer = graph.CreateTransientBuffer("storage", desc);
    CG_REQUIRE(static_cast<bool>(buffer));

    PassData writer{};
    writer.writes[0] = buffer.Value();
    writer.writeStates[0] = ResourceState::ShaderWrite;
    writer.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("buffer writer", writer)).IsOk());
    PassData reader{};
    reader.reads[0] = buffer.Value();
    reader.readStates[0] = ResourceState::ShaderRead;
    reader.readCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("buffer reader", reader, true)).IsOk());
    CG_REQUIRE(graph.Compile().IsOk());

    CommandList commandList;
    CG_REQUIRE(fixture.CreateRecordingList(commandList));
    CG_REQUIRE(graph.Execute(commandList).IsOk());
    CG_EXPECT_TRUE(reader.firstBuffer.IsValid());
    CG_REQUIRE(commandList.End().IsOk());
    const cyber::rhi::CommandListHandle listHandle = commandList.Handle();
    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const cyber::rhi::CommandListHandle>(&listHandle, 1);
    CG_REQUIRE(fixture.device.Submit(submit).IsOk());
    CG_REQUIRE(graph.AcknowledgeSubmission().IsOk());
    CG_EXPECT_EQ(fixture.device.BufferStateOf(reader.firstBuffer), ResourceState::ShaderRead);
    CG_REQUIRE(graph.Destroy().IsOk());
}

CG_TEST(renderer_render_graph_imports_buffers_without_taking_ownership)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());

    cyber::rhi::BufferDesc desc{};
    desc.usage = cyber::rhi::BufferUsage::CopySource | cyber::rhi::BufferUsage::CopyDestination;
    desc.sizeBytes = 128;
    auto created = fixture.device.CreateBuffer(desc);
    CG_REQUIRE(static_cast<bool>(created));

    RenderGraph graph(fixture.device, fixture.heap);
    auto imported = graph.ImportBuffer("external buffer", created.Value());
    CG_REQUIRE(static_cast<bool>(imported));
    CG_EXPECT_TRUE(imported.Value().IsValid());
    auto duplicate = graph.ImportBuffer("duplicate", created.Value());
    CG_EXPECT_FALSE(static_cast<bool>(duplicate));
    CG_EXPECT_EQ(duplicate.Error().code, ErrorCode::AlreadyExists);

    PassData writer{};
    writer.writes[0] = imported.Value();
    writer.writeStates[0] = ResourceState::CopyDestination;
    writer.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("external buffer writer", writer, true)).IsOk());
    CG_REQUIRE(graph.Compile().IsOk());
    CG_EXPECT_FALSE(graph.AcknowledgeSubmission().IsOk());

    CommandList commandList;
    CG_REQUIRE(fixture.CreateRecordingList(commandList));
    CG_REQUIRE(graph.Execute(commandList).IsOk());
    CG_EXPECT_EQ(writer.firstBuffer, created.Value());
    CG_REQUIRE(commandList.End().IsOk());
    const cyber::rhi::CommandListHandle listHandle = commandList.Handle();
    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const cyber::rhi::CommandListHandle>(&listHandle, 1);
    CG_REQUIRE(fixture.device.Submit(submit).IsOk());
    CG_REQUIRE(graph.AcknowledgeSubmission().IsOk());
    CG_EXPECT_EQ(fixture.device.BufferStateOf(created.Value()), ResourceState::CopyDestination);
    CG_REQUIRE(graph.Destroy().IsOk());
    CG_REQUIRE(fixture.device.DestroyBuffer(created.Value()).IsOk());
}

CG_TEST(renderer_render_graph_rejects_reuse_after_unsubmitted_recording)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    RenderGraph graph(fixture.device, fixture.heap);

    TextureDesc desc{};
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::RenderTarget;
    desc.width = 8;
    desc.height = 8;
    auto transient = graph.CreateTransientTexture("recorded transient", desc);
    CG_REQUIRE(static_cast<bool>(transient));
    PassData writer{};
    writer.writes[0] = transient.Value();
    writer.writeStates[0] = ResourceState::RenderTarget;
    writer.writeCount = 1;
    CG_REQUIRE(graph.AddPass(MakePass("record only", writer, true)).IsOk());
    CG_REQUIRE(graph.Compile().IsOk());
    CG_EXPECT_FALSE(graph.AcknowledgeSubmission().IsOk());

    CommandList abandoned;
    CG_REQUIRE(fixture.CreateRecordingList(abandoned));
    CG_REQUIRE(graph.Execute(abandoned).IsOk());
    CG_REQUIRE(abandoned.End().IsOk());

    CommandList retry;
    CG_REQUIRE(fixture.CreateRecordingList(retry));
    const Status<Error> reused = graph.Execute(retry);
    CG_EXPECT_FALSE(reused.IsOk());
    CG_EXPECT_EQ(reused.Error().code, ErrorCode::InvalidArgument);
    abandoned.Reset();
    retry.Reset();
    CG_REQUIRE(graph.Destroy().IsOk());
}
