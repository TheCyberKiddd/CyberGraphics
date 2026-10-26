#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/rhi.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;

CG_TEST(rhi_public_headers)
{
    // Every header under include/cyber/rhi/ compiles on its own; this test exists
    // so a missing include in one of them is a build failure rather than a
    // surprise in a consumer.
    static_assert(!std::is_copy_constructible_v<Device>);
    static_assert(!std::is_copy_assignable_v<Device>);
    static_assert(std::is_move_constructible_v<Device>);
    static_assert(std::is_move_assignable_v<Device>);
    static_assert(std::is_trivially_copyable_v<Capabilities>);
    static_assert(std::is_trivially_copyable_v<AdapterInfo>);
    static_assert(std::is_trivially_copyable_v<Features>);
    static_assert(std::is_trivially_copyable_v<BufferHandle>);
    static_assert(sizeof(BufferHandle) == 8);
    static_assert(sizeof(TextureHandle) == 8);
    static_assert(sizeof(TextureViewHandle) == 8);
    static_assert(sizeof(SamplerHandle) == 8);
    static_assert(std::is_trivially_copyable_v<BufferDesc>);
    static_assert(std::is_trivially_copyable_v<TextureDesc>);
    static_assert(std::is_trivially_copyable_v<TextureViewDesc>);
    static_assert(std::is_trivially_copyable_v<SamplerDesc>);
    static_assert(std::is_trivially_copyable_v<SubresourceRange>);
    static_assert(std::is_trivially_copyable_v<BufferBarrier>);
    static_assert(std::is_trivially_copyable_v<TextureBarrier>);
    static_assert(std::is_trivially_copyable_v<FormatInfo>);
    static_assert(std::is_trivially_copyable_v<BufferInfo>);
    static_assert(std::is_trivially_copyable_v<TextureInfo>);
    static_assert(std::is_trivially_copyable_v<ShaderDesc>);
    static_assert(std::is_trivially_copyable_v<BindGroupLayoutEntry>);
    static_assert(std::is_trivially_copyable_v<BindGroupLayoutDesc>);
    static_assert(std::is_trivially_copyable_v<BindingResource>);
    static_assert(std::is_trivially_copyable_v<PipelineLayoutDesc>);
    static_assert(std::is_trivially_copyable_v<GraphicsPipelineDesc>);
    static_assert(std::is_trivially_copyable_v<ComputePipelineDesc>);
    static_assert(sizeof(PipelineLayoutHandle) == 8);
    static_assert(sizeof(PipelineCacheHandle) == 8);

    // A default device owns nothing and reports so.
    Device device;
    CG_EXPECT_FALSE(device.IsValid());
    CG_EXPECT_STREQ(device.BackendName(), "none");
    CG_EXPECT_FALSE(device.Queue(QueueKind::Graphics).IsValid());
    CG_EXPECT_EQ(device.Capabilities().limits.maxTexture2D, u32{0});
    CG_EXPECT_FALSE(device.Capabilities().features.Has(Feature::Compute));
    CG_EXPECT_FALSE(device.WaitIdle());

    // An empty device rejects every resource call and every query answers a
    // zeroed value rather than reading through a null table.
    CG_EXPECT_FALSE(device.CreateBuffer(BufferDesc{}));
    CG_EXPECT_FALSE(device.CreateTexture(TextureDesc{}));
    CG_EXPECT_FALSE(device.CreateTextureView(TextureViewDesc{}));
    CG_EXPECT_FALSE(device.CreateSampler(SamplerDesc{}));
    CG_EXPECT_FALSE(device.Transition({}, {}));
    CG_EXPECT_FALSE(device.DestroyBuffer(BufferHandle{}));
    CG_EXPECT_FALSE(device.DestroyTexture(TextureHandle{}));
    CG_EXPECT_FALSE(device.DestroyTextureView(TextureViewHandle{}));
    CG_EXPECT_FALSE(device.DestroySampler(SamplerHandle{}));
    CG_EXPECT_FALSE(device.BufferInfoOf(BufferHandle{}).valid);
    CG_EXPECT_FALSE(device.TextureInfoOf(TextureHandle{}).valid);
    CG_EXPECT_FALSE(device.TextureViewInfoOf(TextureViewHandle{}).valid);
    CG_EXPECT_EQ(device.BufferStateOf(BufferHandle{}), ResourceState::Undefined);
    CG_EXPECT_EQ(device.TextureStateOf(TextureHandle{}, 0, 0), ResourceState::Undefined);
    cyber::memory::HeapAllocator heap;
    CG_EXPECT_FALSE(device.CreateShader(ShaderDesc{}));
    CG_EXPECT_FALSE(device.CreateBindGroupLayout(BindGroupLayoutDesc{}));
    CG_EXPECT_FALSE(device.CreateBindGroup(BindGroupDesc{}));
    CG_EXPECT_FALSE(device.CreatePipelineLayout(PipelineLayoutDesc{}));
    CG_EXPECT_FALSE(device.CreateGraphicsPipeline(GraphicsPipelineDesc{}));
    CG_EXPECT_FALSE(device.CreateComputePipeline(ComputePipelineDesc{}));
    CG_EXPECT_FALSE(device.CreatePipelineCache(PipelineCacheDesc{}));
    CG_EXPECT_FALSE(device.PipelineCacheBytes(PipelineCacheHandle{}, heap));

    // The null backend is always available and always the same object.
    const Backend* backend = NullBackend();
    CG_REQUIRE(backend != nullptr);
    CG_EXPECT_EQ(backend, NullBackend());
    CG_EXPECT_EQ(backend->kind, BackendKind::Null);
    CG_EXPECT_STREQ(backend->name, "null");
    CG_EXPECT_TRUE(backend->interface != nullptr);
    CG_EXPECT_TRUE(backend->createDevice != nullptr);
    CG_EXPECT_TRUE(backend->destroyDevice != nullptr);
    CG_EXPECT_TRUE(backend->enumerateAdapters != nullptr);

    // Phase 13 objects are move-only, exactly as the device is, because each owns
    // a slot the device must release exactly once.
    static_assert(!std::is_copy_constructible_v<CommandList>);
    static_assert(!std::is_copy_assignable_v<CommandList>);
    static_assert(std::is_move_constructible_v<CommandList>);
    static_assert(std::is_move_assignable_v<CommandList>);
    static_assert(!std::is_copy_constructible_v<SwapChain>);
    static_assert(std::is_move_constructible_v<SwapChain>);
    static_assert(std::is_trivially_copyable_v<FrameSlot>);
    static_assert(std::is_trivially_copyable_v<SwapChainInfo>);
    static_assert(std::is_trivially_copyable_v<SubmitDesc>);
    static_assert(std::is_trivially_copyable_v<RenderPassInfo>);
    static_assert(std::is_trivially_copyable_v<CommandListStats>);
    static_assert(std::is_trivially_copyable_v<Viewport>);
    static_assert(std::is_trivially_copyable_v<DrawIndexedArguments>);
    static_assert(std::is_trivially_copyable_v<RetireEntry>);
    static_assert(sizeof(CommandListHandle) == 8);
    static_assert(sizeof(SwapChainHandle) == 8);

    // An empty list and an empty swapchain report so and reject every call.
    CommandList list;
    CG_EXPECT_FALSE(list.IsValid());
    CG_EXPECT_EQ(list.State(), CommandListState::Initial);
    CG_EXPECT_EQ(list.Queue(), QueueKind::Graphics);
    CG_EXPECT_FALSE(list.CurrentRenderPass().valid);
    CG_EXPECT_EQ(list.Stats().draws, u32{0});
    CG_EXPECT_FALSE(list.Begin());
    CG_EXPECT_FALSE(list.End());
    list.Reset();

    SwapChain swapchain;
    CG_EXPECT_FALSE(swapchain.IsValid());
    CG_EXPECT_FALSE(swapchain.Info().valid);
    CG_EXPECT_FALSE(swapchain.Present());

    FrameRing ring;
    CG_EXPECT_FALSE(ring.IsValid());
    CG_EXPECT_EQ(ring.FramesInFlight(), u32{0});
    CG_EXPECT_EQ(ring.SignalValueForFrame(0), u64{0});

    // The description validators are pure and safe to call without a device.
    CG_EXPECT_FALSE(ValidateRenderPassDesc(RenderPassDesc{}));
    CG_EXPECT_FALSE(ValidateSubmitDesc(SubmitDesc{}));
    CG_EXPECT_FALSE(ValidateSwapChainDesc(SwapChainDesc{}));
    CG_EXPECT_FALSE(ValidateDrawArguments(DrawArguments{}));
}
