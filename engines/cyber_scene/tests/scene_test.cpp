#include <cyber/jobs/job_system.h>
#include <cyber/math/math.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/scene/scene.h>
#include <cyber/test/test.h>

#include <cstring>
#include <limits>
#include <utility>

using cyber::core::ErrorCode;
using cyber::jobs::JobSystem;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::usize;
using cyber::scene::CameraProxyDesc;
using cyber::scene::MeshInstanceDesc;
using cyber::scene::PointLightDesc;
using cyber::scene::ProxyKind;
using cyber::scene::Scene;
using cyber::scene::SceneCommandWriter;
using cyber::scene::SceneCommandWriterDesc;
using cyber::scene::SceneDesc;
using cyber::scene::SceneView;

namespace
{

[[nodiscard]] MeshInstanceDesc MakeMesh(float x, float z = 5.0f) noexcept
{
    MeshInstanceDesc desc{};
    desc.meshId = 1;
    desc.materialId = 2;
    desc.transform = cyber::math::Mat4::Translation(cyber::math::Vec3{x, 0.0f, z});
    desc.localBounds =
        cyber::math::Aabb::FromCenterExtents(cyber::math::Vec3::Zero(), cyber::math::Vec3{0.5f});
    desc.lodCount = 4;
    desc.lodPixelThresholds[0] = 50.0f;
    desc.lodPixelThresholds[1] = 25.0f;
    desc.lodPixelThresholds[2] = 10.0f;
    return desc;
}

[[nodiscard]] SceneView MakeView() noexcept
{
    SceneView view{};
    view.frustum[cyber::math::FrustumPlane::Left] =
        cyber::math::Plane{cyber::math::Vec3{1.0f, 0.0f, 0.0f}, 1.0f};
    view.frustum[cyber::math::FrustumPlane::Right] =
        cyber::math::Plane{cyber::math::Vec3{-1.0f, 0.0f, 0.0f}, 1.0f};
    view.frustum[cyber::math::FrustumPlane::Bottom] =
        cyber::math::Plane{cyber::math::Vec3{0.0f, 1.0f, 0.0f}, 1.0f};
    view.frustum[cyber::math::FrustumPlane::Top] =
        cyber::math::Plane{cyber::math::Vec3{0.0f, -1.0f, 0.0f}, 1.0f};
    view.frustum[cyber::math::FrustumPlane::Near] =
        cyber::math::Plane{cyber::math::Vec3{0.0f, 0.0f, 1.0f}, -1.0f};
    view.frustum[cyber::math::FrustumPlane::Far] =
        cyber::math::Plane{cyber::math::Vec3{0.0f, 0.0f, -1.0f}, 10.0f};
    view.projectionScalePixels = 100.0f;
    view.nearDistance = 0.1f;
    return view;
}

}  // namespace

CG_TEST(scene_command_writer_limits_and_validation)
{
    HeapAllocator allocator;
    const auto invalid = SceneCommandWriter::Create(SceneCommandWriterDesc{}, allocator);
    CG_EXPECT_FALSE(invalid);
    CG_EXPECT_EQ(invalid.Error().code, ErrorCode::InvalidArgument);

    auto writerResult = SceneCommandWriter::Create(SceneCommandWriterDesc{2, 1}, allocator);
    CG_REQUIRE(writerResult);
    SceneCommandWriter writer = std::move(writerResult.Value());
    MeshInstanceDesc invalidMesh = MakeMesh(0.0f);
    invalidMesh.lodPixelThresholds[1] = invalidMesh.lodPixelThresholds[0];
    const auto rejected = writer.CreateMeshInstance(invalidMesh);
    CG_EXPECT_FALSE(rejected);
    CG_EXPECT_EQ(rejected.Error().code, ErrorCode::InvalidArgument);
    MeshInstanceDesc overflowing = MakeMesh(0.0f);
    overflowing.transform =
        cyber::math::Mat4::Translation({std::numeric_limits<float>::max(), 0.0f, 0.0f});
    overflowing.localBounds = cyber::math::Aabb::FromCenterExtents(
        {}, cyber::math::Vec3{std::numeric_limits<float>::max()});
    const auto overflowRejected = writer.CreateMeshInstance(overflowing);
    CG_EXPECT_FALSE(overflowRejected);
    CG_EXPECT_EQ(overflowRejected.Error().code, ErrorCode::InvalidArgument);

    const auto created = writer.CreateMeshInstance(MakeMesh(0.0f));
    CG_REQUIRE(created);
    const auto full = writer.CreateMeshInstance(MakeMesh(0.0f));
    CG_EXPECT_FALSE(full);
    CG_EXPECT_EQ(full.Error().code, ErrorCode::OutOfRange);
    const auto packet = writer.Seal();
    CG_EXPECT_EQ(writer.CommandCount(), cyber::core::u32{1});
    CG_EXPECT_TRUE(packet.Size() > 0);
    CG_EXPECT_TRUE(packet.Size() <= writer.MaxPacketBytes());
    CG_EXPECT_EQ(packet.Data(), writer.Seal().Data());
    const auto blocked = writer.Destroy(created.Value());
    CG_EXPECT_FALSE(blocked);
    CG_EXPECT_EQ(blocked.Error().code, ErrorCode::Busy);
    CG_EXPECT_TRUE(writer.Commit());
    CG_EXPECT_EQ(writer.CommandCount(), cyber::core::u32{0});
}

CG_TEST(scene_proxy_lifecycle_culling_and_lod)
{
    HeapAllocator allocator;
    auto writerResult = SceneCommandWriter::Create(SceneCommandWriterDesc{8, 16}, allocator);
    auto sceneResult = Scene::Create(SceneDesc{8, 16}, allocator);
    CG_REQUIRE(writerResult);
    CG_REQUIRE(sceneResult);
    SceneCommandWriter writer = std::move(writerResult.Value());
    Scene scene = std::move(sceneResult.Value());

    const auto mesh = writer.CreateMeshInstance(MakeMesh(0.0f));
    CG_REQUIRE(mesh);
    PointLightDesc lightDesc{};
    lightDesc.transform = cyber::math::Mat4::Translation({1.5f, 0.0f, 5.0f});
    lightDesc.range = 0.5f;
    const auto light = writer.CreatePointLight(lightDesc);
    CG_REQUIRE(light);
    const auto camera = writer.CreateCamera(CameraProxyDesc{});
    CG_REQUIRE(camera);
    const auto outside = writer.CreateMeshInstance(MakeMesh(2.1f));
    CG_REQUIRE(outside);
    MeshInstanceDesc rotated = MakeMesh(0.0f);
    rotated.transform = cyber::math::Mat4::Translation({0.0f, 1.5f, 5.0f}) *
                        cyber::math::Mat4::RotationZ(cyber::math::kHalfPi);
    rotated.localBounds =
        cyber::math::Aabb::FromCenterExtents({}, cyber::math::Vec3{2.0f, 0.1f, 0.1f});
    const auto rotatedHandle = writer.CreateMeshInstance(rotated);
    CG_REQUIRE(rotatedHandle);
    const Span<const Byte> packet = writer.Seal();
    CG_EXPECT_TRUE(scene.ApplyCommands(packet));
    CG_EXPECT_TRUE(writer.Commit());
    PointLightDesc movedLight = lightDesc;
    movedLight.range = 0.75f;
    CameraProxyDesc movedCamera{};
    movedCamera.transform = cyber::math::Mat4::Translation({0.0f, 1.0f, 0.0f});
    CG_EXPECT_TRUE(writer.UpdateMeshInstance(mesh.Value(), MakeMesh(0.0f)));
    CG_EXPECT_TRUE(writer.UpdatePointLight(light.Value(), movedLight));
    CG_EXPECT_TRUE(writer.UpdateCamera(camera.Value(), movedCamera));
    const Span<const Byte> updatePacket = writer.Seal();
    CG_EXPECT_TRUE(scene.ApplyCommands(updatePacket));
    CG_EXPECT_TRUE(writer.Commit());
    CG_EXPECT_EQ(scene.ProxyCount(), usize{5});
    CG_EXPECT_EQ(scene.DirtyFlags(mesh.Value()),
                 static_cast<cyber::core::u8>(cyber::scene::kProxyDirtyTransform |
                                              cyber::scene::kProxyDirtyBounds));
    CG_EXPECT_TRUE(scene.AcknowledgeDirty(mesh.Value(), cyber::scene::kProxyDirtyTransform));
    CG_EXPECT_EQ(scene.DirtyFlags(mesh.Value()), cyber::scene::kProxyDirtyBounds);

    auto jobsResult = JobSystem::Create(JobSystem::Desc{2}, allocator);
    CG_REQUIRE(jobsResult);
    JobSystem jobs = std::move(jobsResult.Value());
    const auto visible = scene.Cull(MakeView(), jobs, allocator);
    CG_REQUIRE(visible);
    CG_EXPECT_EQ(visible.Value().Size(), usize{3});
    CG_EXPECT_EQ(visible.Value().Entries()[0].handle, mesh.Value());
    CG_EXPECT_EQ(visible.Value().Entries()[0].kind, ProxyKind::MeshInstance);
    CG_EXPECT_EQ(visible.Value().Entries()[0].lod, cyber::core::u32{2});
    CG_EXPECT_EQ(visible.Value().Entries()[1].handle, light.Value());
    CG_EXPECT_EQ(visible.Value().Entries()[1].kind, ProxyKind::PointLight);
    CG_EXPECT_EQ(visible.Value().Entries()[2].handle, rotatedHandle.Value());
    CG_EXPECT_EQ(visible.Value().Data(), visible.Value().Entries().Data());

    CG_EXPECT_TRUE(writer.Destroy(mesh.Value()));
    const auto staleUpdate = writer.UpdateMeshInstance(mesh.Value(), MakeMesh(0.0f));
    CG_EXPECT_FALSE(staleUpdate);
    const auto newMesh = writer.CreateMeshInstance(MakeMesh(0.25f));
    CG_REQUIRE(newMesh);
    CG_EXPECT_EQ(newMesh.Value().index, mesh.Value().index);
    CG_EXPECT_NE(newMesh.Value().generation, mesh.Value().generation);
    const Span<const Byte> reusePacket = writer.Seal();
    CG_EXPECT_TRUE(scene.ApplyCommands(reusePacket));
    CG_EXPECT_TRUE(writer.Commit());
    CG_EXPECT_EQ(scene.ProxyCount(), usize{5});
    CG_EXPECT_EQ(scene.DirtyFlags(mesh.Value()), cyber::core::u8{0});
    CG_EXPECT_NE(scene.DirtyFlags(newMesh.Value()), cyber::core::u8{0});

    const Byte partialPacket[1]{Byte{0}};
    const auto malformed = scene.ApplyCommands(Span<const Byte>{partialPacket, 1});
    CG_EXPECT_FALSE(malformed);
    CG_EXPECT_TRUE(malformed.Error().code == ErrorCode::ParseError);
    CG_EXPECT_EQ(scene.ProxyCount(), usize{5});
}

CG_TEST(scene_cull_rejects_invalid_view_and_dirty_mask)
{
    HeapAllocator allocator;
    auto writerResult = SceneCommandWriter::Create(SceneCommandWriterDesc{2, 2}, allocator);
    auto sceneResult = Scene::Create(SceneDesc{2, 2}, allocator);
    auto jobsResult = JobSystem::Create(JobSystem::Desc{1}, allocator);
    CG_REQUIRE(writerResult);
    CG_REQUIRE(sceneResult);
    CG_REQUIRE(jobsResult);
    SceneCommandWriter writer = std::move(writerResult.Value());
    Scene scene = std::move(sceneResult.Value());
    JobSystem jobs = std::move(jobsResult.Value());
    const auto mesh = writer.CreateMeshInstance(MakeMesh(0.0f));
    CG_REQUIRE(mesh);
    const Span<const Byte> packet = writer.Seal();
    CG_EXPECT_TRUE(scene.ApplyCommands(packet));
    CG_EXPECT_TRUE(writer.Commit());

    SceneView invalid = MakeView();
    invalid.projectionScalePixels = std::numeric_limits<float>::infinity();
    const auto culled = scene.Cull(invalid, jobs, allocator);
    CG_EXPECT_FALSE(culled);
    CG_EXPECT_EQ(culled.Error().code, ErrorCode::InvalidArgument);
    const auto badMask = scene.AcknowledgeDirty(mesh.Value(), 0x80);
    CG_EXPECT_FALSE(badMask);
    CG_EXPECT_EQ(badMask.Error().code, ErrorCode::InvalidArgument);
    JobSystem emptyJobs;
    const auto noJobs = scene.Cull(MakeView(), emptyJobs, allocator);
    CG_EXPECT_FALSE(noJobs);
    CG_EXPECT_EQ(noJobs.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(scene_rejects_oversized_packets_without_applying_prefix)
{
    HeapAllocator allocator;
    auto writerResult = SceneCommandWriter::Create(SceneCommandWriterDesc{4, 2}, allocator);
    auto sceneResult = Scene::Create(SceneDesc{4, 1}, allocator);
    CG_REQUIRE(writerResult);
    CG_REQUIRE(sceneResult);
    SceneCommandWriter writer = std::move(writerResult.Value());
    Scene scene = std::move(sceneResult.Value());
    CG_REQUIRE(writer.CreateMeshInstance(MakeMesh(0.0f)));
    CG_REQUIRE(writer.CreateMeshInstance(MakeMesh(0.0f)));
    const auto packet = writer.Seal();
    const auto applied = scene.ApplyCommands(packet);
    CG_EXPECT_FALSE(applied);
    CG_EXPECT_TRUE(applied.Error().code == ErrorCode::OutOfRange);
    CG_EXPECT_EQ(scene.ProxyCount(), usize{0});
}

CG_TEST(scene_validates_entire_packet_before_applying)
{
    HeapAllocator allocator;
    auto writerResult = SceneCommandWriter::Create(SceneCommandWriterDesc{2, 2}, allocator);
    auto sceneResult = Scene::Create(SceneDesc{2, 2}, allocator);
    CG_REQUIRE(writerResult);
    CG_REQUIRE(sceneResult);
    SceneCommandWriter writer = std::move(writerResult.Value());
    Scene scene = std::move(sceneResult.Value());
    const auto handle = writer.CreateMeshInstance(MakeMesh(0.0f));
    CG_REQUIRE(handle);
    CG_REQUIRE(writer.UpdateMeshInstance(handle.Value(), MakeMesh(0.25f)));
    const auto packet = writer.Seal();
    CG_REQUIRE(packet.Size() > sizeof(cyber::core::u32));

    void* copy = allocator.Allocate(packet.Size(), alignof(Byte));
    CG_REQUIRE(copy != nullptr);
    std::memcpy(copy, packet.Data(), packet.Size());
    const cyber::core::u32 invalidVersion = 0;
    const usize commandSize = packet.Size() / 2;
    std::memcpy(static_cast<Byte*>(copy) + commandSize, &invalidVersion, sizeof(invalidVersion));
    const auto applied =
        scene.ApplyCommands(Span<const Byte>{static_cast<const Byte*>(copy), packet.Size()});
    allocator.Deallocate(copy);
    CG_EXPECT_FALSE(applied);
    CG_EXPECT_TRUE(applied.Error().code == ErrorCode::ParseError);
    CG_EXPECT_EQ(scene.ProxyCount(), usize{0});
}
