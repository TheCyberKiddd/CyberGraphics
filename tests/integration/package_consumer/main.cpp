#include <cyber/core/build_config.h>
#include <cyber/core/error.h>
#include <cyber/core/hash.h>
#include <cyber/core/log.h>
#include <cyber/core/result.h>
#include <cyber/core/version.h>
#include <cyber/jobs/jobs.h>
#include <cyber/math/math.h>
#include <cyber/memory/memory.h>
#include <cyber/perf/perf.h>
#include <cyber/platform/platform.h>
#include <cyber/software/raster.h>
#include <cyber/software/software.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <string_view>
#include <utility>

namespace
{

struct ConsumerCounter
{
    std::atomic<unsigned> value{0};
};

void ConsumerIncrement(void* userData) noexcept
{
    static_cast<ConsumerCounter*>(userData)->value.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

int main()
{
    using namespace cyber::core;

    const Version version = VersionNumber();
    if (version.major != kVersionMajor)
    {
        return 1;
    }

    const char* text = VersionString();
    if (text == nullptr)
    {
        return 2;
    }

    if (std::strcmp(text, kVersionString) != 0)
    {
        return 3;
    }

    if (BuildConfig::kVersion != std::string_view(kVersionString))
    {
        return 4;
    }

    auto result = Result<int, Error>::Ok(21);
    if (!result.IsOk() || result.Value() != 21)
    {
        return 5;
    }

    auto failed = Result<void, Error>::Err(Error{ErrorCode::NotFound, "missing"});
    if (!failed.IsError() || failed.Error().code != ErrorCode::NotFound)
    {
        return 6;
    }

    if (HashString("abc") != 0x44BC2CF5AD770999ULL)
    {
        return 7;
    }

    Logger logger;
    logger.SetLevel(LogLevel::Info);
    if (!logger.IsEnabled(LogChannel::Core, LogLevel::Info))
    {
        return 8;
    }

    using namespace cyber::math;

    const Vec3 cross = Cross(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    if (LengthSquared(cross - Vec3{0.0f, 0.0f, 1.0f}) > 1.0e-6f)
    {
        return 9;
    }

    const Mat4 transform = Mat4::Translation(Vec3{1.0f, 2.0f, 3.0f}) * Mat4::RotationZ(kHalfPi) *
                           Mat4::Scale(Vec3{2.0f, 2.0f, 2.0f});
    Mat4 inverse;
    if (!transform.TryInverse(inverse))
    {
        return 10;
    }
    const Vec3 origin = Vec3::Zero();
    if (LengthSquared(inverse.TransformPoint(transform.TransformPoint(origin)) - origin) > 1.0e-5f)
    {
        return 11;
    }

    const Quat rotation = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kHalfPi);
    if (!ApproxEqual(rotation.Rotate(Vec3{1.0f, 0.0f, 0.0f}), Vec3{0.0f, 1.0f, 0.0f}, 1.0e-5f))
    {
        return 12;
    }

    if (Half::FromFloat(0.5f).Bits() != static_cast<u16>(0x3800))
    {
        return 13;
    }

    const Mat4 projection = Mat4::PerspectiveReverseZ(kHalfPi, 1.0f, 0.1f, 10.0f);
    if (std::abs(projection.TransformPoint(Vec3{0.0f, 0.0f, -0.1f}).z - 1.0f) > 1.0e-5f)
    {
        return 14;
    }

    const Aabb box = Aabb::FromMinMax(Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f});
    if (!box.Contains(Vec3::Zero()))
    {
        return 15;
    }

    using namespace cyber::memory;

    HeapAllocator heap;
    DynamicArray<int> values(heap);
    if (!values.TryPushBack(7) || values[0] != 7)
    {
        return 16;
    }

    HashMap<StringView, int> lookup(heap);
    if (!lookup.TryInsert(StringView("key"), 42) || *lookup.Find("key") != 42)
    {
        return 17;
    }

    SlotMap<int> slots(heap);
    const Handle<int> handle = slots.Insert(9);
    if (!handle.IsValid() || *slots.Get(handle) != 9)
    {
        return 18;
    }

    using namespace cyber::platform;

    if (Clock::NowSeconds() <= 0.0)
    {
        return 19;
    }

    FrameTimer timer;
    if (timer.Tick() != 0.0 || timer.FrameCount() != 1)
    {
        return 20;
    }

    cyber::perf::Profiler& profiler = cyber::perf::NullProfilerInstance();
    profiler.BeginScope("consumer");
    profiler.EndScope();
    profiler.Counter("consumer", 1.0);

    using namespace cyber::jobs;

    JobSystem::Desc desc{};
    desc.workerCount = 1;
    auto created = JobSystem::Create(desc, heap);
    if (!created)
    {
        return 21;
    }
    JobSystem jobs = std::move(created.Value());

    ConsumerCounter counter;
    JobDesc job{};
    job.name = "consumer";
    job.function = &ConsumerIncrement;
    job.userData = &counter;
    if (!jobs.SubmitDetached(job))
    {
        return 22;
    }
    jobs.WaitIdle();
    if (counter.value.load() != 1)
    {
        return 23;
    }

    jobs.Shutdown();
    if (jobs.IsRunning())
    {
        return 24;
    }

    {
        using namespace cyber::software;

        FramebufferDesc framebufferDesc{};
        framebufferDesc.width = 2;
        framebufferDesc.height = 2;
        auto createdFramebuffer = Framebuffer::Create(framebufferDesc, heap);
        if (!createdFramebuffer)
        {
            return 25;
        }
        Framebuffer framebuffer = std::move(createdFramebuffer.Value());
        framebuffer.Clear(cyber::math::Color{0.5f, 0.5f, 0.5f, 1.0f});

        u8 packed[16] = {};
        if (!PackPresentPixels(framebuffer, Span<u8>(packed, sizeof(packed))))
        {
            return 26;
        }
        if (packed[0] != 188 || packed[3] != 255)
        {
            return 27;
        }

        auto encoded = EncodeImage(framebuffer, ImageWriteDesc{}, heap);
        if (!encoded || encoded.Value().Size() != 23)
        {
            return 28;
        }

        RasterTriangle triangle{};
        triangle.v0 = RasterVertex{Vec2{-1.0f, -1.0f}, Color{1.0f, 0.0f, 0.0f, 1.0f}};
        triangle.v1 = RasterVertex{Vec2{5.0f, -1.0f}, Color{1.0f, 0.0f, 0.0f, 1.0f}};
        triangle.v2 = RasterVertex{Vec2{-1.0f, 5.0f}, Color{1.0f, 0.0f, 0.0f, 1.0f}};
        RasterStats rasterStats{};
        if (!RasterizeTriangles(framebuffer, Span<const RasterTriangle>(&triangle, 1), nullptr,
                                heap, {}, &rasterStats) ||
            rasterStats.pixels != 4)
        {
            return 31;
        }

        // Phase 8: the vertex pipeline clips, culls, and depth-tests through the
        // same installed target.
        DepthBufferDesc depthDesc{};
        depthDesc.width = framebuffer.Width();
        depthDesc.height = framebuffer.Height();
        auto createdDepth = DepthBuffer::Create(depthDesc, heap);
        if (!createdDepth)
        {
            return 32;
        }
        DepthBuffer depth = std::move(createdDepth.Value());
        depth.Clear(0.0f);

        const Vertex quad[6] = {
            Vertex{cyber::math::Vec3{-1.0f, -1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{1.0f, -1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{1.0f, 1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{-1.0f, -1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{1.0f, 1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{-1.0f, 1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
        };
        PipelineDesc pipelineDesc{};
        pipelineDesc.viewport.width = 2.0f;
        pipelineDesc.viewport.height = 2.0f;
        PipelineStats pipelineStats{};
        if (!DrawTriangles(pipelineDesc, Span<const Vertex>(quad), framebuffer, depth, nullptr,
                           heap, &pipelineStats) ||
            pipelineStats.pixels != 4 || depth.Get(1, 1) != 0.5f)
        {
            return 33;
        }

        FramePacer pacer(60);
        if (pacer.TargetFramesPerSecond() != 60)
        {
            return 29;
        }
        if (!IsGlyphSupported('A') || MeasureText("AB").width != 11)
        {
            return 30;
        }
    }

    return 0;
}
