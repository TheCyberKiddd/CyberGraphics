#include <cyber/renderer/bindless_table.h>
#include <cyber/renderer/descriptor_allocator.h>
#include <cyber/renderer/frame_stream.h>
#include <cyber/renderer/hot_pipeline.h>
#include <cyber/renderer/material.h>
#include <cyber/renderer/reflected_layout.h>
#include <cyber/renderer/render_graph.h>
#include <cyber/renderer/render_thread.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::renderer;

CG_TEST(renderer_public_headers)
{
    static_assert(std::is_trivially_copyable_v<ReflectedLayout>);
    static_assert(std::is_trivially_copyable_v<ReflectedVertexLayout>);
    static_assert(std::is_move_constructible_v<MaterialDefinition>);
    static_assert(!std::is_copy_constructible_v<MaterialInstance>);
    static_assert(std::is_move_constructible_v<MaterialInstance>);
    static_assert(!std::is_copy_constructible_v<HotGraphicsPipeline>);
    static_assert(std::is_move_constructible_v<HotGraphicsPipeline>);
    static_assert(!std::is_copy_constructible_v<FrameBufferStream>);
    static_assert(std::is_move_constructible_v<FrameBufferStream>);
    static_assert(!std::is_copy_constructible_v<PersistentDescriptorAllocator>);
    static_assert(std::is_move_constructible_v<PersistentDescriptorAllocator>);
    static_assert(!std::is_copy_constructible_v<TransientDescriptorAllocator>);
    static_assert(std::is_move_constructible_v<TransientDescriptorAllocator>);
    static_assert(!std::is_copy_constructible_v<BindlessResourceTable>);
    static_assert(std::is_move_constructible_v<BindlessResourceTable>);
    static_assert(!std::is_copy_constructible_v<RenderGraph>);
    static_assert(!std::is_move_constructible_v<RenderGraph>);
    static_assert(!std::is_copy_constructible_v<RenderThread>);
    static_assert(std::is_move_constructible_v<RenderThread>);

    CG_EXPECT_EQ(kDefaultRenderFramesInFlight, u32{2});
    CG_EXPECT_EQ(kMaxReflectedPushConstantBytes, u32{128});
    CG_EXPECT_EQ(kMaxMaterialParameters, u32{64});
    CG_EXPECT_EQ(SelectResourceBindingPath(cyber::rhi::Device{}),
                 ResourceBindingPath::Conventional);
    CG_EXPECT_EQ(ToRhiStage(cyber::shaders::Stage::Pixel), cyber::rhi::ShaderStage::Pixel);

    HotGraphicsPipeline empty;
    CG_EXPECT_FALSE(empty.Current().IsValid());
    CG_EXPECT_FALSE(empty.HasBuilt());
    CG_EXPECT_EQ(empty.Layout().bindingCounts[0], u32{0});
    CG_EXPECT_TRUE(empty.LastError() == nullptr);
}
