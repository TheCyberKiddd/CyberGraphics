// CyberGraphics Phase 7 visible rasterization sample.
//
// Builds a jittered 32 x 18 mesh of 1,152 triangles with shared, per-vertex
// colors, rasterizes it with the tile-parallel edge-function path, packs the
// linear framebuffer to sRGB, and presents it through cyber_platform::Presenter.
// The frame loop is paced at 60 Hz and shows the smoothed frame time.
//
// Controls: Escape closes, F11 toggles fullscreen, F2 toggles the overlay.
// `--capture <path>` renders one fixed frame offscreen, writes it twice, and
// fails unless the two files are byte-identical. Capture mode needs no window.
// `--frames <count>` bounds the window run for measurement.

#include <cyber/jobs/job_system.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/platform/platform.h>
#include <cyber/software/software.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

using namespace cyber::platform;
using namespace cyber::software;
using namespace cyber::memory;
using cyber::math::Color;
using cyber::math::Vec2;

namespace
{

constexpr i32 kDefaultWidth = 1280;
constexpr i32 kDefaultHeight = 720;
constexpr i32 kCellsX = 32;
constexpr i32 kCellsY = 18;
constexpr i32 kTileSize = 64;
constexpr usize kVerticesPerRow = static_cast<usize>(kCellsX + 1);
constexpr usize kVertexCount = static_cast<usize>(kCellsX + 1) * (kCellsY + 1);
constexpr usize kTriangleCount = static_cast<usize>(kCellsX) * kCellsY * 2;
constexpr usize kCapturePathBytes = 512;

struct Options
{
    const char* capturePath = nullptr;
    u32 frames = 0;
};

void PrintUsage()
{
    std::printf("usage: cyber_sample_raster [--capture <path.ppm|path.tga>] [--frames <count>]\n");
    std::printf("  Escape closes, F11 toggles fullscreen, F2 toggles the overlay\n");
}

bool ParseOptions(int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--capture") == 0)
        {
            if (index + 1 >= argc)
            {
                return false;
            }
            options.capturePath = argv[++index];
        }
        else if (std::strcmp(argv[index], "--frames") == 0)
        {
            if (index + 1 >= argc)
            {
                return false;
            }
            const char* text = argv[++index];
            char* end = nullptr;
            const unsigned long count = std::strtoul(text, &end, 10);
            if (end == text || *end != '\0' || count == 0 || count > 1000000)
            {
                return false;
            }
            options.frames = static_cast<u32>(count);
        }
        else if (std::strcmp(argv[index], "--help") == 0)
        {
            return false;
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool RecreateFramebuffer(Framebuffer& framebuffer, i32 width, i32 height, Allocator& allocator)
{
    FramebufferDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = PixelFormat::RGBA32F;
    auto created = Framebuffer::Create(desc, allocator);
    if (!created)
    {
        return false;
    }
    framebuffer = std::move(created.Value());
    return true;
}

Color VertexColor(f32 x, f32 y, f32 seconds)
{
    const f32 red = 0.5f + 0.5f * std::sin(x * 0.012f + seconds * 1.1f);
    const f32 green = 0.5f + 0.5f * std::sin(y * 0.014f + seconds * 0.9f + 2.1f);
    const f32 blue = 0.5f + 0.5f * std::sin((x + y) * 0.008f + seconds * 1.4f + 4.2f);
    return Color{red, green, blue, 1.0f};
}

// The outer ring stays on the framebuffer border; interior vertices oscillate by
// a fraction of a cell. Adjacent cells read the same vertex, so the mesh stays a
// watertight partition. Winding is clockwise in screen space.
void BuildMesh(DynamicArray<RasterVertex>& vertices, DynamicArray<RasterTriangle>& triangles,
               i32 width, i32 height, f32 seconds)
{
    const f32 cellWidth = static_cast<f32>(width) / static_cast<f32>(kCellsX);
    const f32 cellHeight = static_cast<f32>(height) / static_cast<f32>(kCellsY);
    const f32 amplitudeX = cellWidth * 0.18f;
    const f32 amplitudeY = cellHeight * 0.18f;

    for (i32 row = 0; row <= kCellsY; ++row)
    {
        for (i32 column = 0; column <= kCellsX; ++column)
        {
            f32 x = static_cast<f32>(column) * cellWidth;
            f32 y = static_cast<f32>(row) * cellHeight;
            if (column > 0 && column < kCellsX)
            {
                x += amplitudeX * std::sin(y * 0.021f + seconds * 1.7f + column * 0.37f);
            }
            if (row > 0 && row < kCellsY)
            {
                y += amplitudeY * std::cos(x * 0.017f + seconds * 1.3f + row * 0.53f);
            }
            vertices[static_cast<usize>(row) * kVerticesPerRow + column] =
                RasterVertex{Vec2{x, y}, VertexColor(x, y, seconds)};
        }
    }

    usize index = 0;
    for (i32 row = 0; row < kCellsY; ++row)
    {
        for (i32 column = 0; column < kCellsX; ++column)
        {
            const usize i00 = static_cast<usize>(row) * kVerticesPerRow + column;
            const usize i10 = i00 + 1;
            const usize i01 = i00 + kVerticesPerRow;
            const usize i11 = i01 + 1;
            triangles[index++] = RasterTriangle{vertices[i00], vertices[i10], vertices[i11]};
            triangles[index++] = RasterTriangle{vertices[i00], vertices[i11], vertices[i01]};
        }
    }
}

void DrawOverlay(Framebuffer& target, const RasterStats& stats, u32 workerCount,
                 f32 frameMilliseconds, f32 framesPerSecond)
{
    char text[64] = {};
    std::snprintf(text, sizeof(text), "%.2f MS  %.1f FPS", static_cast<double>(frameMilliseconds),
                  static_cast<double>(framesPerSecond));
    const TextMetrics metrics = MeasureText(text, 2);
    target.ClearRect(4, 4, metrics.width + 16, metrics.height + 12, Color{0.02f, 0.02f, 0.02f});
    DrawText(target, 10, 10, text, Color{1.0f, 1.0f, 1.0f}, 2);

    DrawText(target, 10, 10 + metrics.height + 8, "PHASE 7  TRIANGLE RASTER",
             Color{0.7f, 0.8f, 1.0f});

    char detail[96] = {};
    std::snprintf(detail, sizeof(detail), "%llu TRIANGLES  %u THREADS",
                  static_cast<unsigned long long>(stats.trianglesDrawn), workerCount);
    DrawText(target, 10, 10 + metrics.height + 18, detail, Color{0.55f, 0.75f, 0.65f});
}

ImageFormat FormatFromPath(const char* path)
{
    const usize length = std::strlen(path);
    if (length >= 4 && (std::strcmp(path + length - 4, ".tga") == 0 ||
                        std::strcmp(path + length - 4, ".TGA") == 0))
    {
        return ImageFormat::Tga;
    }
    return ImageFormat::Ppm;
}

bool BuildCopyPath(const char* path, char (&copyPath)[kCapturePathBytes])
{
    const char* extension = std::strrchr(path, '.');
    const char* slash = std::strrchr(path, '/');
    const char* backslash = std::strrchr(path, '\\');
    const char* lastSeparator =
        backslash != nullptr && (slash == nullptr || backslash > slash) ? backslash : slash;
    if (extension != nullptr && lastSeparator != nullptr && extension < lastSeparator)
    {
        extension = nullptr;
    }

    const usize stemLength =
        extension != nullptr ? static_cast<usize>(extension - path) : std::strlen(path);
    const char* suffix = extension != nullptr ? extension : "";
    const int written = std::snprintf(copyPath, sizeof(copyPath), "%.*s.copy%s",
                                      static_cast<int>(stemLength), path, suffix);
    return written > 0 && static_cast<usize>(written) < sizeof(copyPath);
}

int RunCapture(const char* path, Allocator& allocator)
{
    Framebuffer framebuffer;
    if (!RecreateFramebuffer(framebuffer, 640, 360, allocator))
    {
        std::printf("capture: framebuffer creation failed\n");
        return 1;
    }

    DynamicArray<RasterVertex> vertices(allocator);
    DynamicArray<RasterTriangle> triangles(allocator);
    if (!vertices.TryResize(kVertexCount) || !triangles.TryResize(kTriangleCount))
    {
        std::printf("capture: mesh allocation failed\n");
        return 1;
    }

    BuildMesh(vertices, triangles, framebuffer.Width(), framebuffer.Height(), 1.25f);

    RasterStats stats{};
    RasterizerDesc rasterDesc{};
    rasterDesc.tileSize = kTileSize;
    const auto status = RasterizeTriangles(
        framebuffer, Span<const RasterTriangle>(triangles.Data(), triangles.Size()), nullptr,
        allocator, rasterDesc, &stats);
    if (!status)
    {
        std::printf("capture: rasterization failed: %s\n",
                    cyber::core::ErrorCodeName(status.Error().code));
        return 1;
    }
    DrawOverlay(framebuffer, stats, 1, 16.67f, 60.0f);

    char copyPath[kCapturePathBytes] = {};
    if (!BuildCopyPath(path, copyPath))
    {
        std::printf("capture: path is too long\n");
        return 1;
    }

    ImageWriteDesc desc{};
    desc.format = FormatFromPath(path);
    auto firstWrite = WriteImage(path, framebuffer, allocator, desc);
    auto secondWrite = WriteImage(copyPath, framebuffer, allocator, desc);
    if (!firstWrite || !secondWrite)
    {
        std::printf("capture: image write failed\n");
        return 1;
    }

    auto firstBytes = ReadFile(path, allocator);
    auto secondBytes = ReadFile(copyPath, allocator);
    if (!firstBytes || !secondBytes)
    {
        std::printf("capture: read-back failed\n");
        return 1;
    }

    const bool identical = firstBytes.Value().Size() == secondBytes.Value().Size() &&
                           std::memcmp(firstBytes.Value().Data(), secondBytes.Value().Data(),
                                       firstBytes.Value().Size()) == 0;
    std::printf("capture: %s (%llu bytes) and %s (%llu bytes) are %s\n", path,
                static_cast<unsigned long long>(firstBytes.Value().Size()), copyPath,
                static_cast<unsigned long long>(secondBytes.Value().Size()),
                identical ? "byte-identical" : "DIFFERENT");
    return identical ? 0 : 1;
}

int RunWindow(const Options& options, Allocator& allocator)
{
    if (!IsWindowSystemSupported())
    {
        std::printf("CyberGraphics raster sample: no window backend on this platform.\n");
        std::printf("Use --capture <path> to render a frame offscreen.\n");
        return 0;
    }

    WindowDesc windowDesc{};
    windowDesc.title = "CyberGraphics Phase 7 - triangle rasterization";
    windowDesc.width = kDefaultWidth;
    windowDesc.height = kDefaultHeight;
    windowDesc.resizable = true;
    windowDesc.visible = true;

    auto createdWindow = Window::Create(windowDesc, allocator);
    if (!createdWindow)
    {
        std::printf("window creation failed: %s\n",
                    cyber::core::ErrorCodeName(createdWindow.Error().code));
        return 1;
    }
    Window window = std::move(createdWindow.Value());

    auto createdPresenter = Presenter::Create(window, allocator);
    if (!createdPresenter)
    {
        std::printf("presenter creation failed: %s\n",
                    cyber::core::ErrorCodeName(createdPresenter.Error().code));
        return 1;
    }
    Presenter presenter = std::move(createdPresenter.Value());

    Framebuffer framebuffer;
    if (!RecreateFramebuffer(framebuffer, window.Width(), window.Height(), allocator))
    {
        std::printf("framebuffer creation failed\n");
        return 1;
    }

    DynamicArray<Byte> presentPixels(allocator);
    usize presentSize = PresentByteSize(framebuffer.Width(), framebuffer.Height());
    if (presentSize == 0 || !presentPixels.TryResize(presentSize))
    {
        std::printf("present buffer allocation failed\n");
        return 1;
    }

    DynamicArray<RasterVertex> vertices(allocator);
    DynamicArray<RasterTriangle> triangles(allocator);
    if (!vertices.TryResize(kVertexCount) || !triangles.TryResize(kTriangleCount))
    {
        std::printf("mesh allocation failed\n");
        return 1;
    }

    cyber::jobs::JobSystem jobSystem;
    cyber::jobs::JobSystem::Desc jobDesc{};
    auto createdJobs = cyber::jobs::JobSystem::Create(jobDesc, allocator);
    if (createdJobs)
    {
        jobSystem = std::move(createdJobs.Value());
    }
    else
    {
        std::printf("job system unavailable, using the single-threaded path\n");
    }

    PrintUsage();

    FramePacer pacer(60);
    RasterizerDesc rasterDesc{};
    rasterDesc.tileSize = kTileSize;
    RasterStats lastStats{};

    bool overlay = true;
    bool running = true;
    f32 animationSeconds = 0.0f;
    u64 presentedFrames = 0;
    f64 renderSeconds = 0.0;
    f64 packSeconds = 0.0;
    f64 presentSeconds = 0.0;
    f64 waitSeconds = 0.0;
    f64 pollSeconds = 0.0;

    while (running && window.IsOpen())
    {
        const f64 waitStart = Clock::NowSeconds();
        const f64 frameDelta = pacer.Wait();
        waitSeconds += Clock::NowSeconds() - waitStart;
        animationSeconds += static_cast<f32>(frameDelta > 0.1 ? 0.1 : frameDelta);

        const f64 pollStart = Clock::NowSeconds();
        window.BeginFrame();
        window.PollEvents();

        WindowEvent event{};
        while (window.PollEvent(event))
        {
            switch (event.type)
            {
            case WindowEventType::Close:
                running = false;
                break;
            case WindowEventType::Resize:
                if (event.width > 0 && event.height > 0 &&
                    (event.width != framebuffer.Width() || event.height != framebuffer.Height()))
                {
                    if (!RecreateFramebuffer(framebuffer, event.width, event.height, allocator))
                    {
                        std::printf("framebuffer resize failed\n");
                        running = false;
                        break;
                    }
                    presentSize = PresentByteSize(framebuffer.Width(), framebuffer.Height());
                    if (presentSize == 0 || !presentPixels.TryResize(presentSize))
                    {
                        std::printf("present buffer resize failed\n");
                        running = false;
                    }
                }
                break;
            case WindowEventType::KeyDown:
                if (event.key == Key::Escape)
                {
                    running = false;
                }
                else if (event.key == Key::F11)
                {
                    (void)window.SetFullscreen(!window.IsFullscreen());
                }
                else if (event.key == Key::F2)
                {
                    overlay = !overlay;
                }
                break;
            default:
                break;
            }
            if (!running)
            {
                break;
            }
        }

        if (!running)
        {
            break;
        }
        pollSeconds += Clock::NowSeconds() - pollStart;

        const f64 renderStart = Clock::NowSeconds();
        BuildMesh(vertices, triangles, framebuffer.Width(), framebuffer.Height(), animationSeconds);
        const auto status = RasterizeTriangles(
            framebuffer, Span<const RasterTriangle>(triangles.Data(), triangles.Size()),
            jobSystem.IsRunning() ? &jobSystem : nullptr, allocator, rasterDesc, &lastStats);
        if (!status)
        {
            std::printf("rasterization failed: %s\n",
                        cyber::core::ErrorCodeName(status.Error().code));
            return 1;
        }
        const f64 packStart = Clock::NowSeconds();
        if (overlay)
        {
            DrawOverlay(framebuffer, lastStats,
                        jobSystem.IsRunning() ? jobSystem.WorkerCount() : 1u,
                        static_cast<f32>(pacer.SmoothedFrameSeconds() * 1000.0),
                        static_cast<f32>(pacer.SmoothedFramesPerSecond()));
        }
        renderSeconds += packStart - renderStart;

        auto packed =
            PackPresentPixels(framebuffer, Span<u8>(reinterpret_cast<u8*>(presentPixels.Data()),
                                                    presentPixels.Size()));
        if (!packed)
        {
            std::printf("present packing failed: %s\n",
                        cyber::core::ErrorCodeName(packed.Error().code));
            return 1;
        }
        const f64 presentStart = Clock::NowSeconds();
        packSeconds += presentStart - packStart;

        PresentSource source{};
        source.pixels = Span<const Byte>(presentPixels.Data(), presentPixels.Size());
        source.width = framebuffer.Width();
        source.height = framebuffer.Height();
        source.format = PresentFormat::RGBA8Unorm;

        auto presented = presenter.Present(source);
        presentSeconds += Clock::NowSeconds() - presentStart;
        if (!presented)
        {
            if (!window.IsOpen())
            {
                break;
            }
            std::printf("present failed: %s\n", cyber::core::ErrorCodeName(presented.Error().code));
            return 1;
        }

        ++presentedFrames;
        if (options.frames != 0 && presentedFrames >= options.frames)
        {
            running = false;
        }
    }

    std::printf("raster sample closed after %llu frames, smoothed %.2f ms (%.1f FPS)\n",
                static_cast<unsigned long long>(pacer.FrameCount()),
                pacer.SmoothedFrameSeconds() * 1000.0, pacer.SmoothedFramesPerSecond());
    std::printf("last frame: %llu triangles, %llu pixels, %llu tiles\n",
                static_cast<unsigned long long>(lastStats.trianglesDrawn),
                static_cast<unsigned long long>(lastStats.pixels),
                static_cast<unsigned long long>(lastStats.tiles));
    if (presentedFrames > 0)
    {
        const f64 frames = static_cast<f64>(presentedFrames);
        std::printf("average frame: render %.2f ms, pack %.2f ms, present %.2f ms, "
                    "pacing %.2f ms, events %.2f ms\n",
                    renderSeconds * 1000.0 / frames, packSeconds * 1000.0 / frames,
                    presentSeconds * 1000.0 / frames, waitSeconds * 1000.0 / frames,
                    pollSeconds * 1000.0 / frames);
    }

    jobSystem.Shutdown();
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    Options options{};
    if (!ParseOptions(argc, argv, options))
    {
        PrintUsage();
        return 1;
    }

    HeapAllocator heap;
    if (options.capturePath != nullptr)
    {
        return RunCapture(options.capturePath, heap);
    }
    return RunWindow(options, heap);
}
