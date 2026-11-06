#include <cyber/memory/heap_allocator.h>
#include <cyber/renderer/render_thread.h>
#include <cyber/scene/scene.h>
#include <cyber/test/test.h>

#include <utility>

using cyber::core::Error;
using cyber::core::Status;
using cyber::memory::HeapAllocator;
using cyber::renderer::RenderFrameView;
using cyber::renderer::RenderThread;
using cyber::renderer::RenderThreadDesc;
using cyber::scene::MeshInstanceDesc;
using cyber::scene::Scene;
using cyber::scene::SceneCommandWriter;

namespace
{

struct RenderContext
{
    Scene* scene = nullptr;
};

[[nodiscard]] Status<Error> ApplySceneFrame(const RenderFrameView& frame, void* userData) noexcept
{
    auto& context = *static_cast<RenderContext*>(userData);
    return context.scene->ApplyCommands(frame.data);
}

}  // namespace

CG_TEST(scene_commands_travel_through_render_thread_queue)
{
    HeapAllocator sceneAllocator;
    HeapAllocator writerAllocator;
    HeapAllocator renderAllocator;
    auto sceneResult = Scene::Create({4, 4}, sceneAllocator);
    auto writerResult = SceneCommandWriter::Create({4, 4}, writerAllocator);
    CG_REQUIRE(sceneResult);
    CG_REQUIRE(writerResult);
    Scene scene = std::move(sceneResult.Value());
    SceneCommandWriter writer = std::move(writerResult.Value());
    RenderContext context{&scene};

    RenderThreadDesc desc{};
    desc.allocator = &renderAllocator;
    desc.maxFrameDataBytes = writer.MaxPacketBytes();
    desc.renderFrame = &ApplySceneFrame;
    desc.userData = &context;
    auto renderResult = RenderThread::Create(desc);
    CG_REQUIRE(renderResult);
    RenderThread renderThread = std::move(renderResult.Value());

    MeshInstanceDesc mesh{};
    mesh.meshId = 10;
    mesh.materialId = 20;
    mesh.localBounds =
        cyber::math::Aabb::FromCenterExtents(cyber::math::Vec3::Zero(), cyber::math::Vec3{0.5f});
    const auto handle = writer.CreateMeshInstance(mesh);
    CG_REQUIRE(handle);
    const auto bytes = writer.Seal();
    const auto submitted = renderThread.SubmitFrame(bytes);
    CG_REQUIRE(submitted);
    CG_EXPECT_TRUE(writer.Commit());
    CG_EXPECT_TRUE(renderThread.WaitIdle());
    CG_EXPECT_EQ(scene.ProxyCount(), cyber::memory::usize{1});
    CG_EXPECT_TRUE(renderThread.Shutdown());
}
