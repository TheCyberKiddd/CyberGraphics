// CyberGraphics Phase 9 visible sample: textures, CPU shaders, and lighting.
//
// Renders a rotating cube carrying a checkerboard texture and lit by a
// directional light with Lambert diffuse and a Blinn-Phong highlight. The
// vertex shader is the built-in transform stage; the pixel shader is the
// Lambert/Blinn-Phong shader reading a texture.
//
// Controls: Escape closes, F11 toggles fullscreen. Drag with the left mouse
// button to orbit; the wheel zooms.
//
// `--capture <path.ppm|path.tga>` renders one fixed 640x360 frame offscreen,
// writes it twice, and fails unless the two files are byte-identical.
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
using cyber::core::f32;
using cyber::core::f64;
using cyber::core::i32;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::math::Color;
using cyber::math::Mat4;
using cyber::math::Vec2;
using cyber::math::Vec3;

namespace
{

constexpr i32 kDefaultWidth = 1280;
constexpr i32 kDefaultHeight = 720;
constexpr i32 kCaptureWidth = 640;
constexpr i32 kCaptureHeight = 360;
constexpr usize kCapturePathBytes = 512;

struct Options
{
    const char* capturePath = nullptr;
    u32 frames = 0;
};

struct SceneUniforms
{
    TransformShaderData transform;
    LightingData lighting;
};

void SceneVertex(const VertexInput& input, const void* userData, VertexStageOutput& output) noexcept
{
    TransformVertexShader(input, &static_cast<const SceneUniforms*>(userData)->transform, output);
}

Color ScenePixel(const Varyings& varyings, const void* userData) noexcept
{
    return LambertBlinnPhongPixelShader(varyings,
                                        &static_cast<const SceneUniforms*>(userData)->lighting);
}

void PrintUsage()
{
    std::printf("usage: cyber_sample_textured [--capture <path.ppm|path.tga>] "
                "[--frames <count>]\n");
    std::printf("  drag rotates, wheel zooms, Escape closes, F11 toggles fullscreen\n");
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
            options.frames = static_cast<u32>(std::strtoul(argv[++index], nullptr, 10));
        }
        else
        {
            return false;
        }
    }
    return true;
}

ImageFormat FormatFromPath(const char* path)
{
    const usize length = std::strlen(path);
    if (length >= 4 && (path[length - 1] == 'a' || path[length - 1] == 'A') &&
        (path[length - 2] == 'g' || path[length - 2] == 'G'))
    {
        return ImageFormat::Tga;
    }
    return ImageFormat::Ppm;
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

void BuildCube(Mesh& mesh)
{
    const Vec3 corners[8] = {
        Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, -1.0f},
        Vec3{-1.0f, 1.0f, -1.0f},  Vec3{-1.0f, -1.0f, 1.0f}, Vec3{1.0f, -1.0f, 1.0f},
        Vec3{1.0f, 1.0f, 1.0f},    Vec3{-1.0f, 1.0f, 1.0f},
    };
    struct Face
    {
        Vec3 normal;
        u32 corners[4];
    };
    const Face faces[6] = {
        Face{Vec3{0.0f, 0.0f, 1.0f}, {4, 5, 6, 7}},  Face{Vec3{0.0f, 0.0f, -1.0f}, {1, 0, 3, 2}},
        Face{Vec3{-1.0f, 0.0f, 0.0f}, {0, 4, 7, 3}}, Face{Vec3{1.0f, 0.0f, 0.0f}, {5, 1, 2, 6}},
        Face{Vec3{0.0f, 1.0f, 0.0f}, {7, 6, 2, 3}},  Face{Vec3{0.0f, -1.0f, 0.0f}, {0, 1, 5, 4}},
    };
    const Vec2 uvs[4] = {Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, Vec2{1.0f, 1.0f}, Vec2{0.0f, 1.0f}};

    for (const Face& face : faces)
    {
        const u32 order[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (const auto& triangle : order)
        {
            for (u32 corner = 0; corner < 3; ++corner)
            {
                const u32 slot = triangle[corner];
                VertexInput vertex{};
                vertex.position = corners[face.corners[slot]];
                vertex.normal = face.normal;
                vertex.uv = uvs[slot];
                vertex.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
                (void)mesh.vertices.TryPushBack(vertex);
            }
        }
    }
}

bool MakeCheckerTexture(Texture& texture, Allocator& allocator)
{
    TextureDesc desc{};
    desc.width = 8;
    desc.height = 8;
    desc.format = TextureFormat::RGBA32F;
    desc.generateMips = true;
    auto created = Texture::Create(desc, allocator);
    if (!created)
    {
        return false;
    }
    texture = std::move(created.Value());

    for (i32 y = 0; y < 8; ++y)
    {
        for (i32 x = 0; x < 8; ++x)
        {
            const bool light = ((x / 2) + (y / 2)) % 2 == 0;
            texture.SetTexel(0, x, y,
                             light ? Color{0.90f, 0.78f, 0.55f, 1.0f}
                                   : Color{0.16f, 0.20f, 0.26f, 1.0f});
        }
    }
    texture.GenerateMips();
    return true;
}

void ConfigureScene(SceneUniforms& uniforms, const OrbitCamera& orbit, const Mat4& model,
                    const Texture& texture)
{
    const Vec3 eye = OrbitCameraPosition(orbit);
    uniforms.transform.modelViewProjection =
        OrbitCameraProjection(orbit,
                              static_cast<f32>(kCaptureWidth) / static_cast<f32>(kCaptureHeight)) *
        OrbitCameraView(orbit) * model;
    uniforms.transform.modelToWorld = model;
    uniforms.transform.normalToWorld = model;
    uniforms.transform.eyePosition = eye;

    uniforms.lighting.eyePosition = eye;
    uniforms.lighting.ambientColor = Vec3{0.10f, 0.11f, 0.14f};
    uniforms.lighting.texture = &texture;
    uniforms.lighting.sampler.addressU = AddressMode::Repeat;
    uniforms.lighting.sampler.addressV = AddressMode::Repeat;
    uniforms.lighting.sampler.minFilter = FilterMode::Nearest;
    uniforms.lighting.sampler.magFilter = FilterMode::Nearest;
    uniforms.lighting.mipLevel = 0.0f;
    uniforms.lighting.material.albedo = Color{1.0f, 1.0f, 1.0f, 1.0f};
    uniforms.lighting.material.specular = 0.45f;
    uniforms.lighting.material.shininess = 40.0f;
    uniforms.lighting.material.ambient = 0.30f;
    uniforms.lighting.directional[0].direction =
        cyber::math::Normalize(Vec3{-0.45f, -1.0f, -0.55f});
    uniforms.lighting.directional[0].color = Vec3{1.0f, 0.98f, 0.94f};
    uniforms.lighting.directional[0].intensity = 1.6f;
    uniforms.lighting.directionalCount = 1;
}

bool DrawScene(const Mesh& mesh, SceneUniforms& uniforms, Framebuffer& color, DepthBuffer& depth,
               Allocator& allocator, cyber::jobs::JobSystem* jobSystem, PipelineStats& stats)
{
    ShaderDesc desc{};
    desc.pipeline.viewport =
        Viewport{0.0f, 0.0f, static_cast<f32>(color.Width()), static_cast<f32>(color.Height())};
    desc.vertexShader = &SceneVertex;
    desc.pixelShader = &ScenePixel;
    desc.userData = &uniforms;
    return static_cast<bool>(DrawShadedTriangles(
        desc, Span<const VertexInput>(mesh.vertices.Data(), mesh.vertices.Size()), color, depth,
        jobSystem, allocator, &stats));
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

    Mesh mesh(allocator);
    BuildCube(mesh);
    Texture texture;
    if (!MakeCheckerTexture(texture, allocator))
    {
        std::printf("capture: texture creation failed\n");
        return 1;
    }

    OrbitCamera orbit;
    orbit.target = Vec3{0.0f, 0.0f, 0.0f};
    orbit.distance = 4.5f;
    orbit.yawRadians = 0.65f;
    orbit.pitchRadians = 0.42f;
    orbit.nearPlane = 0.1f;
    orbit.farPlane = 200.0f;

    SceneUniforms uniforms{};
    ConfigureScene(uniforms, orbit, Mat4::RotationY(0.4f), texture);

    framebuffer.Clear(Color{0.03f, 0.04f, 0.06f, 1.0f});
    depth.Clear(0.0f);
    PipelineStats stats{};
    if (!DrawScene(mesh, uniforms, framebuffer, depth, allocator, nullptr, stats))
    {
        std::printf("capture: draw failed\n");
        return 1;
    }
    std::printf("capture: %llu submitted, %llu culled, %llu clipped, %llu pixels\n",
                static_cast<unsigned long long>(stats.trianglesSubmitted),
                static_cast<unsigned long long>(stats.trianglesCulled),
                static_cast<unsigned long long>(stats.trianglesClipped),
                static_cast<unsigned long long>(stats.pixels));

    char copyPath[kCapturePathBytes] = {};
    const usize pathLength = std::strlen(options.capturePath);
    if (pathLength + 6 >= kCapturePathBytes)
    {
        std::printf("capture: path is too long\n");
        return 1;
    }
    std::memcpy(copyPath, options.capturePath, pathLength);
    std::memcpy(copyPath + pathLength, ".copy", 5);
    copyPath[pathLength + 5] = '\0';

    ImageWriteDesc writeDesc{};
    writeDesc.format = FormatFromPath(options.capturePath);
    writeDesc.encodeSrgb = true;
    auto firstWrite = WriteImage(options.capturePath, framebuffer, allocator, writeDesc);
    auto secondWrite = WriteImage(copyPath, framebuffer, allocator, writeDesc);
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
    std::printf("capture: %s and %s are %s\n", options.capturePath, copyPath,
                identical ? "byte-identical" : "DIFFERENT");
    return identical ? 0 : 1;
}

int RunWindow(const Options& options, Allocator& allocator)
{
    if (!IsWindowSystemSupported())
    {
        std::printf("CyberGraphics textured sample: no window backend on this platform.\n");
        std::printf("Use --capture <path> to render a frame offscreen.\n");
        return 0;
    }

    WindowDesc windowDesc{};
    windowDesc.title = "CyberGraphics Phase 9 - textures, shaders, lighting";
    windowDesc.width = kDefaultWidth;
    windowDesc.height = kDefaultHeight;
    windowDesc.resizable = true;
    windowDesc.visible = true;

    auto createdWindow = Window::Create(windowDesc, allocator);
    if (!createdWindow)
    {
        std::printf("window creation failed\n");
        return 1;
    }
    Window window = std::move(createdWindow.Value());

    auto createdPresenter = Presenter::Create(window, allocator);
    if (!createdPresenter)
    {
        std::printf("presenter creation failed\n");
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

    Mesh mesh(allocator);
    BuildCube(mesh);
    Texture texture;
    if (!MakeCheckerTexture(texture, allocator))
    {
        std::printf("texture creation failed\n");
        return 1;
    }

    cyber::jobs::JobSystem jobSystem;
    cyber::jobs::JobSystem::Desc jobDesc{};
    auto createdJobs = cyber::jobs::JobSystem::Create(jobDesc, allocator);
    if (createdJobs)
    {
        jobSystem = std::move(createdJobs.Value());
    }

    PrintUsage();

    OrbitCamera orbit;
    orbit.target = Vec3{0.0f, 0.0f, 0.0f};
    orbit.distance = 4.5f;
    orbit.yawRadians = 0.65f;
    orbit.pitchRadians = 0.42f;
    orbit.nearPlane = 0.1f;
    orbit.farPlane = 200.0f;

    FramePacer pacer(60);
    bool running = true;
    f32 animationSeconds = 0.0f;
    u64 presentedFrames = 0;

    while (running && window.IsOpen())
    {
        const f64 frameDelta = pacer.Wait();
        animationSeconds += static_cast<f32>(frameDelta > 0.1 ? 0.1 : frameDelta);

        window.BeginFrame();
        window.PollEvents();
        const InputState& input = window.Input();

        WindowEvent event{};
        while (window.PollEvent(event))
        {
            if (event.type == WindowEventType::Close)
            {
                running = false;
            }
            else if (event.type == WindowEventType::Resize && event.width > 0 && event.height > 0 &&
                     (event.width != framebuffer.Width() || event.height != framebuffer.Height()))
            {
                if (!CreateTargets(framebuffer, depth, event.width, event.height, allocator))
                {
                    running = false;
                    break;
                }
                presentSize = PresentByteSize(framebuffer.Width(), framebuffer.Height());
                if (presentSize == 0 || !presentPixels.TryResize(presentSize))
                {
                    running = false;
                }
            }
            else if (event.type == WindowEventType::KeyDown)
            {
                if (event.key == Key::Escape)
                {
                    running = false;
                }
                else if (event.key == Key::F11)
                {
                    (void)window.SetFullscreen(!window.IsFullscreen());
                }
            }
        }
        if (!running)
        {
            break;
        }

        OrbitCameraInput orbitInput;
        if (input.IsMouseButtonDown(MouseButton::Left))
        {
            orbitInput.yawDelta = -input.MouseDeltaX() * 0.005f;
            orbitInput.pitchDelta = -input.MouseDeltaY() * 0.005f;
        }
        orbitInput.zoomDelta = input.WheelDelta() * 0.15f;
        UpdateOrbitCamera(orbit, orbitInput, static_cast<f32>(frameDelta));

        framebuffer.Clear(Color{0.03f, 0.04f, 0.06f, 1.0f});
        depth.Clear(0.0f);

        SceneUniforms uniforms{};
        const Mat4 model = Mat4::RotationY(animationSeconds * 0.6f);
        ConfigureScene(uniforms, orbit, model, texture);

        PipelineStats stats{};
        if (!DrawScene(mesh, uniforms, framebuffer, depth, allocator,
                       jobSystem.IsRunning() ? &jobSystem : nullptr, stats))
        {
            std::printf("draw failed\n");
            return 1;
        }

        auto packed =
            PackPresentPixels(framebuffer, Span<u8>(reinterpret_cast<u8*>(presentPixels.Data()),
                                                    presentPixels.Size()));
        if (!packed)
        {
            std::printf("present packing failed\n");
            return 1;
        }

        PresentSource source{};
        source.pixels = Span<const Byte>(presentPixels.Data(), presentPixels.Size());
        source.width = framebuffer.Width();
        source.height = framebuffer.Height();
        source.format = PresentFormat::RGBA8Unorm;
        auto presented = presenter.Present(source);
        if (!presented)
        {
            if (!window.IsOpen())
            {
                break;
            }
            std::printf("present failed\n");
            return 1;
        }

        ++presentedFrames;
        if (options.frames != 0 && presentedFrames >= options.frames)
        {
            running = false;
        }
    }

    std::printf("textured sample closed after %llu frames, smoothed %.2f ms (%.1f FPS)\n",
                static_cast<unsigned long long>(pacer.FrameCount()),
                pacer.SmoothedFrameSeconds() * 1000.0, pacer.SmoothedFramesPerSecond());
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    Options options{};
    if (!ParseOptions(argc, argv, options))
    {
        PrintUsage();
        return 2;
    }

    HeapAllocator allocator;
    if (options.capturePath != nullptr)
    {
        return RunCapture(options, allocator);
    }
    return RunWindow(options, allocator);
}
