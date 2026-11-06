#include <cyber/jobs/job_system.h>
#include <cyber/math/math.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/scene/scene.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <utility>

using cyber::jobs::JobSystem;
using cyber::memory::HeapAllocator;
using cyber::memory::usize;
using cyber::scene::MeshInstanceDesc;
using cyber::scene::Scene;
using cyber::scene::SceneCommandWriter;

namespace
{

constexpr cyber::core::u32 kObjectCount = 100000;

[[nodiscard]] MeshInstanceDesc MakeMesh(cyber::core::u32 index) noexcept
{
    const float x = static_cast<float>(index % 400U) * 0.5f - 100.0f;
    const float y = static_cast<float>((index / 400U) % 250U) * 0.5f - 62.5f;
    const float z = 10.0f + static_cast<float>(index % 31U);
    MeshInstanceDesc desc{};
    desc.meshId = 1;
    desc.materialId = 1;
    desc.transform = cyber::math::Mat4::Translation({x, y, z});
    desc.localBounds = cyber::math::Aabb::FromCenterExtents({}, cyber::math::Vec3{0.1f});
    return desc;
}

[[nodiscard]] cyber::scene::SceneView MakeView() noexcept
{
    using cyber::math::Plane;
    using cyber::math::Vec3;
    cyber::scene::SceneView view{};
    view.frustum[cyber::math::FrustumPlane::Left] = Plane{Vec3{1.0f, 0.0f, 0.0f}, 10.0f};
    view.frustum[cyber::math::FrustumPlane::Right] = Plane{Vec3{-1.0f, 0.0f, 0.0f}, 10.0f};
    view.frustum[cyber::math::FrustumPlane::Bottom] = Plane{Vec3{0.0f, 1.0f, 0.0f}, 10.0f};
    view.frustum[cyber::math::FrustumPlane::Top] = Plane{Vec3{0.0f, -1.0f, 0.0f}, 10.0f};
    view.frustum[cyber::math::FrustumPlane::Near] = Plane{Vec3{0.0f, 0.0f, 1.0f}, -1.0f};
    view.frustum[cyber::math::FrustumPlane::Far] = Plane{Vec3{0.0f, 0.0f, -1.0f}, 60.0f};
    view.projectionScalePixels = 720.0f;
    return view;
}

}  // namespace

int main()
{
    HeapAllocator allocator;
    auto writerResult = SceneCommandWriter::Create({kObjectCount, kObjectCount}, allocator);
    auto sceneResult = Scene::Create({kObjectCount, kObjectCount}, allocator);
    auto jobsResult = JobSystem::Create(JobSystem::Desc{4}, allocator);
    if (!writerResult || !sceneResult || !jobsResult)
    {
        std::fprintf(stderr, "failed to initialize 100,000-proxy benchmark\n");
        return 1;
    }
    SceneCommandWriter writer = std::move(writerResult.Value());
    Scene scene = std::move(sceneResult.Value());
    JobSystem jobs = std::move(jobsResult.Value());

    for (cyber::core::u32 index = 0; index < kObjectCount; ++index)
    {
        if (!writer.CreateMeshInstance(MakeMesh(index)))
        {
            std::fprintf(stderr, "failed to create proxy %u\n", index);
            return 1;
        }
    }
    const auto packet = writer.Seal();
    if (!scene.ApplyCommands(packet) || !writer.Commit())
    {
        std::fprintf(stderr, "failed to apply benchmark proxy packet\n");
        return 1;
    }

    const cyber::scene::SceneView view = MakeView();
    for (cyber::core::u32 warmup = 0; warmup < 2; ++warmup)
    {
        auto visible = scene.Cull(view, jobs, allocator);
        if (!visible)
        {
            std::fprintf(stderr, "cull warmup failed: %s\n", visible.Error().message);
            return 1;
        }
    }

    std::array<long long, 21> samples{};
    cyber::core::usize referenceCount = 0;
    for (usize sample = 0; sample < samples.size(); ++sample)
    {
        const auto start = std::chrono::steady_clock::now();
        auto visible = scene.Cull(view, jobs, allocator);
        const auto stop = std::chrono::steady_clock::now();
        if (!visible)
        {
            std::fprintf(stderr, "cull failed: %s\n", visible.Error().message);
            return 1;
        }
        samples[sample] =
            std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();

        usize visibleIndex = 0;
        for (cyber::core::u32 index = 0; index < kObjectCount; ++index)
        {
            const MeshInstanceDesc desc = MakeMesh(index);
            const cyber::math::Vec3 center =
                desc.transform.TransformPoint(desc.localBounds.Center());
            const cyber::math::Aabb bounds =
                cyber::math::Aabb::FromCenterExtents(center, desc.localBounds.Extents());
            if (cyber::math::Intersects(view.frustum, bounds))
            {
                if (visibleIndex >= visible.Value().Size() ||
                    visible.Value().Entries()[visibleIndex].handle.index != index ||
                    visible.Value().Entries()[visibleIndex].handle.generation != 1 ||
                    visible.Value().Entries()[visibleIndex].kind !=
                        cyber::scene::ProxyKind::MeshInstance)
                {
                    std::fprintf(stderr, "parallel cull differs from brute-force result\n");
                    return 2;
                }
                ++visibleIndex;
            }
        }
        if (sample == 0)
        {
            referenceCount = visibleIndex;
        }
        if (visibleIndex != visible.Value().Size() || visibleIndex != referenceCount)
        {
            std::fprintf(stderr, "parallel cull visibility count is inconsistent\n");
            return 2;
        }
    }

    std::sort(samples.begin(), samples.end());
    std::printf("objects=%u visible=%zu reference=%zu workers=%u median_us=%lld p95_us=%lld\n",
                kObjectCount, referenceCount, referenceCount, jobs.WorkerCount(), samples[10],
                samples[19]);
    return scene.ProxyCount() == kObjectCount ? 0 : 2;
}
