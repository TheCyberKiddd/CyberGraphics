// Phase 15 increment 4 (ADR 0018): debug names and pass markers.
//
// A capture is only readable when resources carry names and passes carry
// markers. Nothing here reads the backend's bookkeeping: a name is asked for
// through `Device::ObjectName`, whose D3D12 implementation reads the driver's
// own name slot back (`GetPrivateData` under WKPDID_D3DDebugObjectNameW), which
// is the only honest way to test a name — query it back. The markers are
// checked through the command list's own counters, which is where the RHI
// records what it emitted.
//
// The name part runs on the D3D12 backend because that is where names reach a
// driver; a device without an adapter reports the skip. The marker part also
// runs on the validation backend, where the same RHI path must behave the same
// way with no driver behind it at all.

// Name read-back is a device query, so it needs a device; on a machine with no
// D3D12 adapter the marker path still runs on the validation backend in the
// test below it.

#include "rhi_fixture.h"

#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/texture.h>
#include <cyber/test/test.h>

#include <cstring>
#include <utility>

using namespace cyber::rhi;
using namespace cyber::rhi::test;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

// A device on the last adapter: WARP when the machine has no GPU. The wrapper
// is in front of the backend, so the read-back crosses the same layers a
// capture does.
[[nodiscard]] bool OpenD3D12Device(Device& out, HeapAllocator& heap) noexcept
{
    const Backend* backend = D3D12Backend();
    if (backend == nullptr)
    {
        return false;
    }
    const u32 adapters = backend->enumerateAdapters(nullptr, 0);
    if (adapters == 0)
    {
        return false;
    }
    DeviceDesc desc{};
    desc.backend = backend;
    desc.adapterIndex = adapters - 1;
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

// Compares read-back text against what was given, truncating the way the fixed
// `ObjectNameInfo` buffer does.
[[nodiscard]] bool NameMatches(const ObjectNameInfo& info, const char* label) noexcept
{
    if (!info.named)
    {
        return false;
    }
    const u32 asked = static_cast<u32>(std::strlen(label));
    const u32 held = info.length < sizeof(info.name) - 1 ? info.length : sizeof(info.name) - 1;
    if (asked > held || std::strncmp(info.name, label, held) != 0)
    {
        return false;
    }
    return info.name[info.length] == '\0';
}

// Every kind one device can carry a label through, converted to the generic
// ref the read-back query takes. A kind the backend cannot name still goes
// through the entry, so the matrix is stated in one place.
[[nodiscard]] ObjectRef RefOf(ObjectKind kind, u64 packed) noexcept
{
    ObjectRef ref{};
    ref.kind = kind;
    ref.packed = packed;
    return ref;
}

}  // namespace

CG_TEST(rhi_debug_markers_names_are_read_back_through_the_driver)
{
    HeapAllocator heap;
    Device device;
    if (!OpenD3D12Device(device, heap))
    {
        CG_SKIP("this build has no Direct3D 12 adapter to name objects on");
    }
    // The labelled family: a buffer, a texture, a fence, and a command list,
    // each named in its own descriptor.
    BufferDesc bufferDesc{};
    bufferDesc.usage = BufferUsage::Vertex;
    bufferDesc.sizeBytes = 256;
    bufferDesc.label = "marked vertex buffer";
    auto buffer = device.CreateBuffer(bufferDesc);
    CG_REQUIRE(static_cast<bool>(buffer));

    TextureDesc textureDesc{};
    textureDesc.dimension = TextureDimension::Dim2D;
    textureDesc.format = Format::RGBA8Unorm;
    textureDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
    textureDesc.width = 16;
    textureDesc.height = 16;
    textureDesc.label = "marked target";
    auto texture = device.CreateTexture(textureDesc);
    CG_REQUIRE(static_cast<bool>(texture));

    FenceDesc fenceDesc{};
    fenceDesc.label = "marked frame gate";
    auto fence = device.CreateFence(fenceDesc);
    CG_REQUIRE(static_cast<bool>(fence));

    CommandListDesc listDesc{};
    listDesc.queue = QueueKind::Graphics;
    listDesc.label = "marked recorder";
    auto list = device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(list));

    // What the driver has: the same words, read back through the name slot
    // written at creation.
    CG_EXPECT_TRUE(
        NameMatches(device.ObjectName(RefOf(ObjectKind::Buffer, PackHandle(buffer.Value()))),
                    "marked vertex buffer"));
    CG_EXPECT_TRUE(
        NameMatches(device.ObjectName(RefOf(ObjectKind::Texture, PackHandle(texture.Value()))),
                    "marked target"));
    CG_EXPECT_TRUE(
        NameMatches(device.ObjectName(RefOf(ObjectKind::Fence, PackHandle(fence.Value()))),
                    "marked frame gate"));
    CG_EXPECT_TRUE(NameMatches(
        device.ObjectName(RefOf(ObjectKind::CommandList, PackHandle(list.Value().Handle()))),
        "marked recorder"));

    // Kinds without a single driver object behind them (a sampler) and a stale
    // handle answer unnamed rather than inventing a name.
    SamplerDesc samplerDesc{};
    samplerDesc.label = "a sampler cannot hide";
    auto sampler = device.CreateSampler(samplerDesc);
    CG_REQUIRE(static_cast<bool>(sampler));
    CG_EXPECT_FALSE(
        device.ObjectName(RefOf(ObjectKind::Sampler, PackHandle(sampler.Value()))).named);

    BufferHandle stale = buffer.Value();
    CG_EXPECT_TRUE(device.DestroyBuffer(buffer.Value()));
    CG_EXPECT_FALSE(device.ObjectName(RefOf(ObjectKind::Buffer, PackHandle(stale))).named);

    // Destroying what is left through the owning objects.
    CG_EXPECT_TRUE(device.DestroyTexture(texture.Value()));
    CG_EXPECT_TRUE(device.DestroyFence(fence.Value()));
}

CG_TEST(rhi_debug_markers_passes_carry_markers_from_the_rhi)
{
    HeapAllocator heap;

    // The validation backend: the marker path is the RHI's, so it must behave
    // identically with no driver behind it.
    Fixture fixture;
    CG_REQUIRE(fixture.Build(32, 32));

    auto opened = fixture.device.CreateCommandList(CommandListDesc{});
    CG_REQUIRE(static_cast<bool>(opened));
    CommandList list = std::move(opened.Value());
    CG_REQUIRE(list.Begin());
    CG_REQUIRE(fixture.TransitionTargets(list));

    // The pass's label comes from the descriptor, and the counters reflect what
    // the RHI emitted: one region for the pass, one for the caller's own label
    // nested inside it, and both closed by their ends in order.
    ColorAttachment color[1] = {};
    color[0].view = fixture.colorView;
    color[0].loadOp = LoadOp::Clear;
    color[0].storeOp = StoreOp::Store;
    color[0].clearValue = ClearColorValue::Of(0.0f, 0.0f, 0.0f, 1.0f);

    RenderPassDesc passDesc{};
    passDesc.colorAttachments = Span<const ColorAttachment>(color, 1);
    passDesc.label = "shadow pass";
    CG_EXPECT_TRUE(list.BeginRenderPass(passDesc));
    CG_EXPECT_EQ(list.Stats().debugLabels, 1U);
    CG_EXPECT_EQ(list.Stats().renderPasses, 1U);

    CG_EXPECT_TRUE(list.BeginDebugLabel("atlases"));
    CG_EXPECT_EQ(list.Stats().debugLabels, 2U);
    CG_EXPECT_TRUE(list.SetDebugMarker("shadow atlas ready"));
    CG_EXPECT_TRUE(list.EndDebugLabel());
    CG_EXPECT_TRUE(list.EndRenderPass());
    CG_EXPECT_EQ(list.Stats().debugLabels, 2U);
    CG_EXPECT_EQ(list.Stats().renderPasses, 1U);

    // A pass without a label gets no event, and no error: markers are the
    // caller's choice, not the renderer's obligation.
    RenderPassDesc silentDesc{};
    silentDesc.colorAttachments = Span<const ColorAttachment>(color, 1);
    CG_EXPECT_TRUE(list.BeginRenderPass(silentDesc));
    CG_EXPECT_EQ(list.Stats().debugLabels, 2U);
    CG_EXPECT_TRUE(list.EndRenderPass());

    // Labels nest inside the still-open pass the same way a caller writes them.
    CG_EXPECT_TRUE(list.BeginRenderPass(passDesc));
    CG_EXPECT_TRUE(list.BeginDebugLabel("clipping"));
    CG_EXPECT_EQ(list.Stats().debugLabels, 4U);
    CG_EXPECT_TRUE(list.EndRenderPass());
    CG_EXPECT_TRUE(list.EndDebugLabel());
    CG_EXPECT_EQ(list.Stats().debugLabels, 4U);

    CG_EXPECT_TRUE(list.End());
}

CG_TEST(rhi_debug_markers_d3d12_advertises_native_capture_support_when_enabled)
{
    HeapAllocator heap;
    Device device;
    if (!OpenD3D12Device(device, heap))
    {
        CG_SKIP("this build has no Direct3D 12 adapter to test");
    }
#if defined(CG_DEBUG) || defined(CG_PROFILE)
    CG_EXPECT_TRUE(device.Capabilities().features.Has(Feature::DebugMarkers));
#else
    CG_EXPECT_FALSE(device.Capabilities().features.Has(Feature::DebugMarkers));
#endif

    TextureDesc colorDesc{};
    colorDesc.dimension = TextureDimension::Dim2D;
    colorDesc.format = Format::RGBA8Unorm;
    colorDesc.usage = TextureUsage::RenderTarget;
    colorDesc.width = 16;
    colorDesc.height = 16;
    colorDesc.label = "marked target";
    auto texture = device.CreateTexture(colorDesc);
    CG_REQUIRE(static_cast<bool>(texture));

    TextureViewDesc viewDesc{};
    viewDesc.texture = texture.Value();
    viewDesc.format = Format::RGBA8Unorm;
    auto view = device.CreateTextureView(viewDesc);
    CG_REQUIRE(static_cast<bool>(view));

    CommandListDesc listDesc{};
    listDesc.queue = QueueKind::Graphics;
    listDesc.label = "marked recorder";
    auto opened = device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(opened));
    CommandList list = std::move(opened.Value());
    CG_REQUIRE(list.Begin());
    CG_EXPECT_TRUE(list.SetDebugMarker("shadow pass begin"));

    ColorAttachment color[1] = {};
    color[0].view = view.Value();
    color[0].loadOp = LoadOp::Clear;
    color[0].storeOp = StoreOp::Store;
    color[0].clearValue = ClearColorValue::Of(0.25f, 0.5f, 1.0f, 1.0f);

    RenderPassDesc passDesc{};
    passDesc.colorAttachments = Span<const ColorAttachment>(color, 1);
    passDesc.label = "shadow pass";
    CG_EXPECT_TRUE(list.BeginRenderPass(passDesc));
    CG_EXPECT_TRUE(list.EndRenderPass());
    CG_EXPECT_EQ(list.Stats().debugLabels, 1U);
    CG_EXPECT_EQ(list.Stats().renderPasses, 1U);
    CG_EXPECT_TRUE(list.End());

    // The list's own driver name is read back after marker emission.
    CG_EXPECT_TRUE(
        NameMatches(device.ObjectName(RefOf(ObjectKind::CommandList, PackHandle(list.Handle()))),
                    "marked recorder"));
}

CG_TEST(rhi_debug_markers_releasing_a_list_with_an_open_pass_reads_balanced)
{
    HeapAllocator heap;

    Fixture fixture;
    CG_REQUIRE(fixture.Build(32, 32));

    {
        auto opened = fixture.device.CreateCommandList(CommandListDesc{});
        CG_REQUIRE(static_cast<bool>(opened));
        CommandList list = std::move(opened.Value());
        CG_REQUIRE(list.Begin());
        CG_REQUIRE(fixture.TransitionTargets(list));

        ColorAttachment color[1] = {};
        color[0].view = fixture.colorView;
        color[0].loadOp = LoadOp::Clear;
        color[0].storeOp = StoreOp::Store;
        color[0].clearValue = ClearColorValue::Of(0.0f, 0.0f, 0.0f, 1.0f);

        RenderPassDesc passDesc{};
        passDesc.colorAttachments = Span<const ColorAttachment>(color, 1);
        passDesc.label = "leaked marker pass";
        CG_EXPECT_TRUE(list.BeginRenderPass(passDesc));
        CG_EXPECT_TRUE(list.BeginDebugLabel("nested"));
        // Releasing here drops the recording; nothing dangles because the
        // label state lives on the list, not in the backend's tables.
    }
}
