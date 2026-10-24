// CyberGraphics Phase 8 visible sample: depth, clipping, and perspective.
//
// Renders a rotating, depth-tested cube over a checkerboard floor that recedes
// to the horizon. The floor shows perspective-correct interpolation, the cube
// shows back-face culling and the reverse-Z depth buffer, and the clipper keeps
// the image stable when the camera moves through the near plane.
//
// Controls: Escape closes, F11 toggles fullscreen, F2 toggles the overlay,
// Tab toggles orbit and fly cameras. Drag with the left mouse button to rotate,
// the wheel to zoom. In fly mode WASD moves, Q/E move down/up, and Shift is
// fast.
//
// `--capture <path.ppm|path.tga>` renders one fixed frame offscreen, writes it
// twice, and fails unless the two files are byte-identical. `--inside` captures
// the camera at the centre of the cube, which exercises near-plane clipping.
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
using cyber::math::Mat4;
using cyber::math::Vec3;

namespace
{

constexpr i32 kDefaultWidth = 1280;
constexpr i32 kDefaultHeight = 720;
constexpr i32 kCaptureWidth = 640;
constexpr i32 kCaptureHeight = 360;
constexpr i32 kFloorCells = 24;
constexpr f32 kFloorExtent = 12.0f;
constexpr f32 kFloorHeight = -1.0f;
constexpr f32 kCaptureSeconds = 1.25f;
constexpr usize kCapturePathBytes = 512;
constexpr usize kCubeTriangleCount = 12;
constexpr usize kCubeVertexCount = kCubeTriangleCount * 3;
constexpr usize kFloorTriangleCount = static_cast<usize>(kFloorCells) * kFloorCells * 2;
constexpr usize kSceneTriangleCount = kCubeTriangleCount + kFloorTriangleCount;
constexpr usize kSceneVertexCount = kSceneTriangleCount * 3;

struct Options
{
    const char* capturePath = nullptr;
    u32 frames = 0;
    bool inside = false;
};

// Which controller drives the camera. The sample maps platform input to plain
// delta structs; the renderer never sees a key code.
enum class CameraMode
{
    Orbit,
    Fly,
};

void PrintUsage()
{
    std::printf("usage: cyber_sample_depth [--capture <path.ppm|path.tga>] [--inside] "
                "[--frames <count>]\n");
    std::printf("  Tab toggles orbit/fly, drag rotates, wheel zooms\n");
    std::printf("  WASD moves in fly mode, Q/E move down/up, Shift is fast\n");
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
        else if (std::strcmp(argv[index], "--inside") == 0)
        {
            options.inside = true;
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

bool CreateTargets(Framebuffer& color, DepthBuffer& depth, i32 width, i32 height,
                   Allocator& allocator)
{
    FramebufferDesc colorDesc{};
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = PixelFormat::RGBA32F;
    auto createdColor = Framebuffer::Create(colorDesc, allocator);
    if (!createdColor)
    {
        return false;
    }
    color = std::move(createdColor.Value());

    DepthBufferDesc depthDesc{};
    depthDesc.width = width;
    depthDesc.height = height;
    auto createdDepth = DepthBuffer::Create(depthDesc, allocator);
    if (!createdDepth)
    {
        return false;
    }
    depth = std::move(createdDepth.Value());
    return true;
}

// Both builders assume the caller reserved kSceneVertexCount, so a push after
// that reservation cannot allocate and its result cannot fail.

// Writes the unit cube as twelve triangles wound counter-clockwise when seen
// from outside, with a distinct color per face so culling is visible.
void AppendCube(DynamicArray<Vertex>& vertices, f32 rotationSeconds)
{
    const Vec3 corners[8] = {
        Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, -1.0f},
        Vec3{-1.0f, 1.0f, -1.0f},  Vec3{-1.0f, -1.0f, 1.0f}, Vec3{1.0f, -1.0f, 1.0f},
        Vec3{1.0f, 1.0f, 1.0f},    Vec3{-1.0f, 1.0f, 1.0f},
    };
    const int faces[36] = {
        4, 5, 6, 4, 6, 7,  // +Z
        1, 0, 3, 1, 3, 2,  // -Z
        0, 4, 7, 0, 7, 3,  // -X
        5, 1, 2, 5, 2, 6,  // +X
        7, 6, 2, 7, 2, 3,  // +Y
        0, 1, 5, 0, 5, 4,  // -Y
    };
    const Color faceColors[6] = {
        Color{0.90f, 0.45f, 0.30f, 1.0f}, Color{0.30f, 0.55f, 0.90f, 1.0f},
        Color{0.40f, 0.80f, 0.45f, 1.0f}, Color{0.85f, 0.75f, 0.30f, 1.0f},
        Color{0.75f, 0.40f, 0.85f, 1.0f}, Color{0.35f, 0.75f, 0.80f, 1.0f},
    };

    const Mat4 model =
        Mat4::RotationY(rotationSeconds * 0.6f) * Mat4::RotationX(rotationSeconds * 0.27f);
    for (usize index = 0; index < kCubeVertexCount; ++index)
    {
        const int face = static_cast<int>(index) / 6;
        const Vec3 local = corners[faces[index]];
        (void)vertices.TryPushBack(Vertex{model.TransformPoint(local), faceColors[face]});
    }
}

// Checkerboard floor in the XZ plane, two counter-clockwise-from-above
// triangles per cell. Shared cell corners guarantee watertight coverage.
void AppendFloor(DynamicArray<Vertex>& vertices)
{
    const f32 cellSize = (kFloorExtent * 2.0f) / static_cast<f32>(kFloorCells);
    const Color light{0.42f, 0.44f, 0.48f, 1.0f};
    const Color dark{0.22f, 0.23f, 0.27f, 1.0f};

    for (i32 cellZ = 0; cellZ < kFloorCells; ++cellZ)
    {
        for (i32 cellX = 0; cellX < kFloorCells; ++cellX)
        {
            const f32 x0 = -kFloorExtent + static_cast<f32>(cellX) * cellSize;
            const f32 z0 = -kFloorExtent + static_cast<f32>(cellZ) * cellSize;
            const f32 x1 = x0 + cellSize;
            const f32 z1 = z0 + cellSize;
            const Color color = ((cellX + cellZ) % 2 == 0) ? light : dark;

            const Vertex p00{Vec3{x0, kFloorHeight, z0}, color};
            const Vertex p01{Vec3{x0, kFloorHeight, z1}, color};
            const Vertex p11{Vec3{x1, kFloorHeight, z1}, color};
            const Vertex p10{Vec3{x1, kFloorHeight, z0}, color};

            (void)vertices.TryPushBack(p00);
            (void)vertices.TryPushBack(p01);
            (void)vertices.TryPushBack(p11);
            (void)vertices.TryPushBack(p00);
            (void)vertices.TryPushBack(p11);
            (void)vertices.TryPushBack(p10);
        }
    }
}

void DrawOverlay(Framebuffer& target, const PipelineStats& stats, const char* modeText,
                 f32 frameMilliseconds, f32 framesPerSecond)
{
    char line[96] = {};
    std::snprintf(line, sizeof(line), "%.2f MS  %.1f FPS", static_cast<double>(frameMilliseconds),
                  static_cast<double>(framesPerSecond));
    const TextMetrics metrics = MeasureText(line, 2);
    target.ClearRect(4, 4, metrics.width + 16, metrics.height + 12, Color{0.02f, 0.02f, 0.03f});
    DrawText(target, 10, 10, line, Color{1.0f, 1.0f, 1.0f}, 2);

    DrawText(target, 10, 10 + metrics.height + 8, "PHASE 8  DEPTH  CLIP  PERSPECTIVE",
             Color{0.70f, 0.80f, 1.0f});

    char detail[128] = {};
    std::snprintf(detail, sizeof(detail), "%s  %llu TRIANGLES  %llu CULLED", modeText,
                  static_cast<unsigned long long>(stats.trianglesSubmitted),
                  static_cast<unsigned long long>(stats.trianglesCulled));
    DrawText(target, 10, 10 + metrics.height + 18, detail, Color{0.55f, 0.80f, 0.65f});
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

int RunCapture(const Options& options, Allocator& allocator)
{
    Framebuffer framebuffer;
    DepthBuffer depth;
    if (!CreateTargets(framebuffer, depth, kCaptureWidth, kCaptureHeight, allocator))
    {
        std::printf("capture: target creation failed\n");
        return 1;
    }

    DynamicArray<Vertex> scene(allocator);
    if (!scene.TryReserve(kSceneVertexCount))
    {
        std::printf("capture: scene allocation failed\n");
        return 1;
    }
    AppendCube(scene, kCaptureSeconds);
    AppendFloor(scene);
    if (scene.Size() != kSceneVertexCount)
    {
        std::printf("capture: scene is incomplete\n");
        return 1;
    }

    framebuffer.Clear(Color{0.05f, 0.07f, 0.10f, 1.0f});
    depth.Clear(0.0f);

    OrbitCamera orbit;
    orbit.target = Vec3{0.0f, 0.0f, 0.0f};
    orbit.distance = 6.0f;
    orbit.yawRadians = 0.85f;
    orbit.pitchRadians = 0.42f;
    orbit.nearPlane = 0.1f;
    orbit.farPlane = 200.0f;

    PipelineDesc desc{};
    desc.viewport.width = static_cast<f32>(kCaptureWidth);
    desc.viewport.height = static_cast<f32>(kCaptureHeight);
    desc.view = OrbitCameraView(orbit);
    desc.projection = OrbitCameraProjection(orbit, static_cast<f32>(kCaptureWidth) /
                                                       static_cast<f32>(kCaptureHeight));
    // NDC depth from the projection already carries the cube and floor; the
    // model matrix is already applied by AppendCube.
    if (options.inside)
    {
        Camera interior;
        interior.position = Vec3{0.0f, 0.0f, 0.0f};
        interior.yawRadians = 0.35f;
        interior.pitchRadians = 0.15f;
        interior.nearPlane = 0.05f;
        interior.farPlane = 200.0f;
        desc.view = CameraView(interior);
        desc.projection = CameraProjection(interior, static_cast<f32>(kCaptureWidth) /
                                                         static_cast<f32>(kCaptureHeight));
        // From inside the cube every face is back-facing; disabling culling
        // shows the interior surfaces and exercises the near clipper.
        desc.cullMode = CullMode::None;
    }

    PipelineStats stats{};
    const auto status = DrawTriangles(desc, Span<const Vertex>(scene.Data(), scene.Size()),
                                      framebuffer, depth, nullptr, allocator, &stats);
    if (!status)
    {
        std::printf("capture: draw failed: %s\n", cyber::core::ErrorCodeName(status.Error().code));
        return 1;
    }

    DrawOverlay(framebuffer, stats, options.inside ? "INSIDE" : "ORBIT", 16.67f, 60.0f);
    std::printf("capture: %llu triangles submitted, %llu culled, %llu clipped, %llu pixels\n",
                static_cast<unsigned long long>(stats.trianglesSubmitted),
                static_cast<unsigned long long>(stats.trianglesCulled),
                static_cast<unsigned long long>(stats.trianglesClipped),
                static_cast<unsigned long long>(stats.pixels));

    char copyPath[kCapturePathBytes] = {};
    if (!BuildCopyPath(options.capturePath, copyPath))
    {
        std::printf("capture: path is too long\n");
        return 1;
    }

    ImageWriteDesc desc2{};
    desc2.format = FormatFromPath(options.capturePath);
    auto firstWrite = WriteImage(options.capturePath, framebuffer, allocator, desc2);
    auto secondWrite = WriteImage(copyPath, framebuffer, allocator, desc2);
    if (!firstWrite || !secondWrite)
    {
        std::printf("capture: image write failed\n");
        return 1;
    }

    auto firstBytes = ReadFile(options.capturePath, allocator);
    auto secondBytes = ReadFile(copyPath, allocator);
    if (!firstBytes || !secondBytes)
    {
        std::printf("capture: read-back failed\n");
        return 1;
    }

    const bool identical = firstBytes.Value().Size() == secondBytes.Value().Size() &&
                           std::memcmp(firstBytes.Value().Data(), secondBytes.Value().Data(),
                                       firstBytes.Value().Size()) == 0;
    std::printf("capture: %s (%llu bytes) and %s (%llu bytes) are %s\n", options.capturePath,
                static_cast<unsigned long long>(firstBytes.Value().Size()), copyPath,
                static_cast<unsigned long long>(secondBytes.Value().Size()),
                identical ? "byte-identical" : "DIFFERENT");
    return identical ? 0 : 1;
}

int RunWindow(const Options& options, Allocator& allocator)
{
    if (!IsWindowSystemSupported())
    {
        std::printf("CyberGraphics depth sample: no window backend on this platform.\n");
        std::printf("Use --capture <path> to render a frame offscreen.\n");
        return 0;
    }

    WindowDesc windowDesc{};
    windowDesc.title = "CyberGraphics Phase 8 - depth, clipping, perspective";
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
    DepthBuffer depth;
    if (!CreateTargets(framebuffer, depth, window.Width(), window.Height(), allocator))
    {
        std::printf("target creation failed\n");
        return 1;
    }

    DynamicArray<Byte> presentPixels(allocator);
    usize presentSize = PresentByteSize(framebuffer.Width(), framebuffer.Height());
    if (presentSize == 0 || !presentPixels.TryResize(presentSize))
    {
        std::printf("present buffer allocation failed\n");
        return 1;
    }

    DynamicArray<Vertex> scene(allocator);
    if (!scene.TryReserve(kSceneVertexCount))
    {
        std::printf("scene allocation failed\n");
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

    OrbitCamera orbit;
    orbit.target = Vec3{0.0f, 0.0f, 0.0f};
    orbit.distance = 6.0f;
    orbit.yawRadians = 0.85f;
    orbit.pitchRadians = 0.42f;
    orbit.nearPlane = 0.1f;
    orbit.farPlane = 200.0f;

    FlyCamera fly;
    fly.moveSpeed = 6.0f;

    CameraMode mode = CameraMode::Orbit;
    FramePacer pacer(60);
    PipelineStats lastStats{};
    bool overlay = true;
    bool running = true;
    f32 animationSeconds = 0.0f;
    u64 presentedFrames = 0;
    f64 sceneSeconds = 0.0;
    f64 clearSeconds = 0.0;
    f64 drawSeconds = 0.0;
    f64 packSeconds = 0.0;
    f64 presentSeconds = 0.0;
    f64 waitSeconds = 0.0;
    f64 pollSeconds = 0.0;

    const auto switchMode = [&mode, &orbit, &fly]()
    {
        if (mode == CameraMode::Orbit)
        {
            const Vec3 eye = OrbitCameraPosition(orbit);
            fly.camera.position = eye;
            fly.camera.yawRadians = orbit.yawRadians;
            fly.camera.pitchRadians = -orbit.pitchRadians;
            fly.camera.nearPlane = orbit.nearPlane;
            fly.camera.farPlane = orbit.farPlane;
            mode = CameraMode::Fly;
        }
        else
        {
            const Vec3 eye = fly.camera.position;
            const Vec3 offset = eye - orbit.target;
            const f32 distance = cyber::math::Length(offset);
            orbit.distance = distance > orbit.minDistance ? distance : orbit.minDistance;
            orbit.yawRadians = std::atan2(offset.x, offset.z);
            orbit.pitchRadians = std::asin(offset.y / (distance > 0.0f ? distance : 1.0f));
            mode = CameraMode::Orbit;
        }
    };

    while (running && window.IsOpen())
    {
        const f64 waitStart = Clock::NowSeconds();
        const f64 frameDelta = pacer.Wait();
        waitSeconds += Clock::NowSeconds() - waitStart;
        animationSeconds += static_cast<f32>(frameDelta > 0.1 ? 0.1 : frameDelta);

        const f64 pollStart = Clock::NowSeconds();
        window.BeginFrame();
        window.PollEvents();
        const InputState& input = window.Input();

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
                    if (!CreateTargets(framebuffer, depth, event.width, event.height, allocator))
                    {
                        std::printf("target resize failed\n");
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
                else if (event.key == Key::Tab)
                {
                    switchMode();
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

        if (mode == CameraMode::Orbit)
        {
            OrbitCameraInput orbitInput;
            if (input.IsMouseButtonDown(MouseButton::Left))
            {
                orbitInput.yawDelta = -input.MouseDeltaX() * 0.005f;
                orbitInput.pitchDelta = -input.MouseDeltaY() * 0.005f;
            }
            orbitInput.zoomDelta = input.WheelDelta() * 0.15f;
            UpdateOrbitCamera(orbit, orbitInput, static_cast<f32>(frameDelta));
        }
        else
        {
            FlyCameraInput flyInput;
            if (input.IsMouseButtonDown(MouseButton::Left))
            {
                flyInput.yawDelta = -input.MouseDeltaX() * 0.004f;
                flyInput.pitchDelta = -input.MouseDeltaY() * 0.004f;
            }
            flyInput.moveForward =
                (input.IsKeyDown(Key::W) ? 1.0f : 0.0f) - (input.IsKeyDown(Key::S) ? 1.0f : 0.0f);
            flyInput.moveRight =
                (input.IsKeyDown(Key::D) ? 1.0f : 0.0f) - (input.IsKeyDown(Key::A) ? 1.0f : 0.0f);
            flyInput.moveUp =
                (input.IsKeyDown(Key::E) ? 1.0f : 0.0f) - (input.IsKeyDown(Key::Q) ? 1.0f : 0.0f);
            flyInput.fast = input.IsKeyDown(Key::Shift);
            UpdateFlyCamera(fly, flyInput, static_cast<f32>(frameDelta));
        }

        const f64 sceneStart = Clock::NowSeconds();
        scene.Clear();
        AppendCube(scene, animationSeconds);
        AppendFloor(scene);
        const f64 clearStart = Clock::NowSeconds();
        sceneSeconds += clearStart - sceneStart;

        framebuffer.Clear(Color{0.05f, 0.07f, 0.10f, 1.0f});
        depth.Clear(0.0f);
        const f64 drawStart = Clock::NowSeconds();
        clearSeconds += drawStart - clearStart;

        PipelineDesc desc{};
        desc.viewport.width = static_cast<f32>(framebuffer.Width());
        desc.viewport.height = static_cast<f32>(framebuffer.Height());
        if (mode == CameraMode::Orbit)
        {
            desc.view = OrbitCameraView(orbit);
            desc.projection =
                OrbitCameraProjection(orbit, static_cast<f32>(framebuffer.Width()) /
                                                 static_cast<f32>(framebuffer.Height()));
        }
        else
        {
            desc.view = CameraView(fly.camera);
            desc.projection =
                CameraProjection(fly.camera, static_cast<f32>(framebuffer.Width()) /
                                                 static_cast<f32>(framebuffer.Height()));
        }

        const auto status =
            DrawTriangles(desc, Span<const Vertex>(scene.Data(), scene.Size()), framebuffer, depth,
                          jobSystem.IsRunning() ? &jobSystem : nullptr, allocator, &lastStats);
        if (!status)
        {
            std::printf("draw failed: %s\n", cyber::core::ErrorCodeName(status.Error().code));
            return 1;
        }

        const f64 packStart = Clock::NowSeconds();
        if (overlay)
        {
            DrawOverlay(framebuffer, lastStats, mode == CameraMode::Orbit ? "ORBIT" : "FLY",
                        static_cast<f32>(pacer.SmoothedFrameSeconds() * 1000.0),
                        static_cast<f32>(pacer.SmoothedFramesPerSecond()));
        }
        drawSeconds += packStart - drawStart;

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

    std::printf("depth sample closed after %llu frames, smoothed %.2f ms (%.1f FPS)\n",
                static_cast<unsigned long long>(pacer.FrameCount()),
                pacer.SmoothedFrameSeconds() * 1000.0, pacer.SmoothedFramesPerSecond());
    std::printf("last frame: %llu submitted, %llu culled, %llu clipped, %llu pixels, "
                "%llu rejected\n",
                static_cast<unsigned long long>(lastStats.trianglesSubmitted),
                static_cast<unsigned long long>(lastStats.trianglesCulled),
                static_cast<unsigned long long>(lastStats.trianglesClipped),
                static_cast<unsigned long long>(lastStats.pixels),
                static_cast<unsigned long long>(lastStats.pixelsRejected));
    if (presentedFrames > 0)
    {
        const f64 frames = static_cast<f64>(presentedFrames);
        std::printf("average frame: scene %.2f ms, clear %.2f ms, draw %.2f ms, "
                    "pack %.2f ms, present %.2f ms, pacing %.2f ms, events %.2f ms\n",
                    sceneSeconds * 1000.0 / frames, clearSeconds * 1000.0 / frames,
                    drawSeconds * 1000.0 / frames, packSeconds * 1000.0 / frames,
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
        return RunCapture(options, heap);
    }
    return RunWindow(options, heap);
}
