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
// `--supersample <1-4>` renders that many samples per axis and box-filters the
// result down, which is the sample's antialiasing path. The software
// rasterizer has no multisample coverage yet, so supersampling is how a
// silhouette is smoothed: 1 is aliased, 4 is reference quality.

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
using cyber::core::i64;
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
// Lowest and highest samples per axis the supersampler accepts.
constexpr i32 kMinSupersample = 1;
constexpr i32 kMaxSupersample = 4;
// Ceiling on the offscreen render target in the interactive path. A window
// larger than this renders at a lower sample count so the frame rate does not
// collapse; the capture path is not bounded because it renders one frame.
constexpr i64 kMaxWindowRenderPixels = 2560 * 1440;
constexpr i64 kMaxFramebufferDimension = 16384;

struct Options
{
    const char* capturePath = nullptr;
    u32 frames = 0;
    i32 supersample = 2;
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
                "[--frames <count>] [--supersample <1-4>]\n");
    std::printf("  drag rotates, wheel zooms, Escape closes, F11 toggles fullscreen\n");
    std::printf("  supersample is samples per axis: 1 aliased, 4 reference quality\n");
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
        else if (std::strcmp(argv[index], "--supersample") == 0)
        {
            if (index + 1 >= argc)
            {
                return false;
            }
            const long value = std::strtol(argv[++index], nullptr, 10);
            if (value < kMinSupersample || value > kMaxSupersample)
            {
                return false;
            }
            options.supersample = static_cast<i32>(value);
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

bool CreateColorTarget(Framebuffer& color, i32 width, i32 height, Allocator& allocator)
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
    return true;
}

bool CreateTargets(Framebuffer& color, DepthBuffer& depth, i32 width, i32 height,
                   Allocator& allocator)
{
    if (!CreateColorTarget(color, width, height, allocator))
    {
        return false;
    }

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

// Vertical studio backdrop: brighter above the horizon, near-black below. A flat
// fill reads as a viewport default rather than a rendered frame.
void FillBackground(Framebuffer& color) noexcept
{
    const Color top{0.040f, 0.049f, 0.068f, 1.0f};
    const Color bottom{0.006f, 0.007f, 0.011f, 1.0f};
    const i32 height = color.Height();
    const f32 inverseHeight = height > 1 ? 1.0f / static_cast<f32>(height - 1) : 0.0f;
    for (i32 y = 0; y < height; ++y)
    {
        const f32 t = static_cast<f32>(y) * inverseHeight;
        color.ClearRect(0, y, color.Width(), 1,
                        Color{top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t,
                              top.b + (bottom.b - top.b) * t, 1.0f});
    }
}

// Box-filters an integer supersampled source down by `factor` samples per axis.
// Averaging happens in linear space before the sRGB transfer function, which is
// what makes the result the mean radiance of the covered samples rather than a
// brighter one. Returns false when the sizes do not divide exactly.
bool ResolveDownsample(const Framebuffer& source, Framebuffer& destination, i32 factor) noexcept
{
    if (!source.IsValid() || !destination.IsValid() || factor < 1)
    {
        return false;
    }
    if (source.Width() != destination.Width() * factor ||
        source.Height() != destination.Height() * factor)
    {
        return false;
    }

    const f32 inverseSamples = 1.0f / static_cast<f32>(factor * factor);
    for (i32 y = 0; y < destination.Height(); ++y)
    {
        for (i32 x = 0; x < destination.Width(); ++x)
        {
            f32 red = 0.0f;
            f32 green = 0.0f;
            f32 blue = 0.0f;
            f32 alpha = 0.0f;
            for (i32 sampleY = 0; sampleY < factor; ++sampleY)
            {
                for (i32 sampleX = 0; sampleX < factor; ++sampleX)
                {
                    const Color texel = source.GetPixel(x * factor + sampleX, y * factor + sampleY);
                    red += texel.r;
                    green += texel.g;
                    blue += texel.b;
                    alpha += texel.a;
                }
            }
            destination.SetPixel(x, y,
                                 Color{red * inverseSamples, green * inverseSamples,
                                       blue * inverseSamples, alpha * inverseSamples});
        }
    }
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
    // Eight checks per face from a 256-texel square. A block has to be many
    // texels wide: bilinear magnification blends over one texel, so a coarse
    // source with two-texel blocks smears each border across a quarter of the
    // check and reads as a soft blur rather than an edge.
    constexpr i32 kSize = 256;
    constexpr i32 kBlock = 32;
    TextureDesc desc{};
    desc.width = kSize;
    desc.height = kSize;
    desc.format = TextureFormat::RGBA32F;
    desc.generateMips = true;
    auto created = Texture::Create(desc, allocator);
    if (!created)
    {
        return false;
    }
    texture = std::move(created.Value());

    for (i32 y = 0; y < kSize; ++y)
    {
        for (i32 x = 0; x < kSize; ++x)
        {
            const bool light = ((x / kBlock) + (y / kBlock)) % 2 == 0;
            texture.SetTexel(0, x, y,
                             light ? Color{0.90f, 0.78f, 0.55f, 1.0f}
                                   : Color{0.16f, 0.20f, 0.26f, 1.0f});
        }
    }
    texture.GenerateMips();
    return true;
}

void ConfigureScene(SceneUniforms& uniforms, const OrbitCamera& orbit, const Mat4& model,
                    const Texture& texture, f32 aspect)
{
    const Vec3 eye = OrbitCameraPosition(orbit);
    uniforms.transform.modelViewProjection =
        OrbitCameraProjection(orbit, aspect) * OrbitCameraView(orbit) * model;
    uniforms.transform.modelToWorld = model;
    uniforms.transform.normalToWorld = model;
    uniforms.transform.eyePosition = eye;

    uniforms.lighting.eyePosition = eye;
    uniforms.lighting.ambientColor = Vec3{0.10f, 0.11f, 0.14f};
    uniforms.lighting.texture = &texture;
    uniforms.lighting.sampler.addressU = AddressMode::Repeat;
    uniforms.lighting.sampler.addressV = AddressMode::Repeat;
    uniforms.lighting.sampler.minFilter = FilterMode::Bilinear;
    uniforms.lighting.sampler.magFilter = FilterMode::Bilinear;
    uniforms.lighting.sampler.mipFilter = FilterMode::Bilinear;
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
    const i32 factor = options.supersample;
    const i32 renderWidth = kCaptureWidth * factor;
    const i32 renderHeight = kCaptureHeight * factor;
    if (renderWidth > kMaxFramebufferDimension || renderHeight > kMaxFramebufferDimension)
    {
        std::printf("capture: supersampled size exceeds the framebuffer limit\n");
        return 1;
    }

    Framebuffer framebuffer;
    DepthBuffer depth;
    if (!CreateTargets(framebuffer, depth, renderWidth, renderHeight, allocator))
    {
        std::printf("capture: target creation failed\n");
        return 1;
    }

    Framebuffer resolved;
    if (!CreateColorTarget(resolved, kCaptureWidth, kCaptureHeight, allocator))
    {
        std::printf("capture: resolve target creation failed\n");
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
    ConfigureScene(uniforms, orbit, Mat4::RotationY(0.4f), texture,
                   static_cast<f32>(kCaptureWidth) / static_cast<f32>(kCaptureHeight));

    FillBackground(framebuffer);
    depth.Clear(0.0f);
    PipelineStats stats{};
    if (!DrawScene(mesh, uniforms, framebuffer, depth, allocator, nullptr, stats))
    {
        std::printf("capture: draw failed\n");
        return 1;
    }
    if (!ResolveDownsample(framebuffer, resolved, factor))
    {
        std::printf("capture: resolve failed\n");
        return 1;
    }
    std::printf("capture: %llu submitted, %llu culled, %llu clipped, %llu pixels "
                "(%dx%d supersampled %dx, resolved to %dx%d)\n",
                static_cast<unsigned long long>(stats.trianglesSubmitted),
                static_cast<unsigned long long>(stats.trianglesCulled),
                static_cast<unsigned long long>(stats.trianglesClipped),
                static_cast<unsigned long long>(stats.pixels), renderWidth, renderHeight, factor,
                kCaptureWidth, kCaptureHeight);

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
    auto firstWrite = WriteImage(options.capturePath, resolved, allocator, writeDesc);
    auto secondWrite = WriteImage(copyPath, resolved, allocator, writeDesc);
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
    Framebuffer resolved;
    DepthBuffer depth;
    i32 factor = 1;
    // Allocates the supersampled render target plus the present-resolution
    // target, lowering the sample count until the render fits the interactive
    // pixel ceiling. Returns false when either allocation fails.
    const auto recreateTargets = [&](i32 width, i32 height) -> bool
    {
        i32 samples = options.supersample;
        while (samples > 1 &&
               static_cast<i64>(width) * samples * height * samples > kMaxWindowRenderPixels)
        {
            --samples;
        }
        if (!CreateTargets(framebuffer, depth, width * samples, height * samples, allocator))
        {
            return false;
        }
        if (!CreateColorTarget(resolved, width, height, allocator))
        {
            return false;
        }
        factor = samples;
        return true;
    };

    if (!recreateTargets(window.Width(), window.Height()))
    {
        std::printf("target creation failed\n");
        return 1;
    }

    DynamicArray<Byte> presentPixels(allocator);
    usize presentSize = PresentByteSize(resolved.Width(), resolved.Height());
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
                     (event.width != resolved.Width() || event.height != resolved.Height()))
            {
                if (!recreateTargets(event.width, event.height))
                {
                    running = false;
                    break;
                }
                presentSize = PresentByteSize(resolved.Width(), resolved.Height());
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

        FillBackground(framebuffer);
        depth.Clear(0.0f);

        SceneUniforms uniforms{};
        const Mat4 model = Mat4::RotationY(animationSeconds * 0.6f);
        ConfigureScene(uniforms, orbit, model, texture,
                       static_cast<f32>(resolved.Width()) / static_cast<f32>(resolved.Height()));

        PipelineStats stats{};
        if (!DrawScene(mesh, uniforms, framebuffer, depth, allocator,
                       jobSystem.IsRunning() ? &jobSystem : nullptr, stats))
        {
            std::printf("draw failed\n");
            return 1;
        }
        if (!ResolveDownsample(framebuffer, resolved, factor))
        {
            std::printf("resolve failed\n");
            return 1;
        }

        auto packed = PackPresentPixels(
            resolved, Span<u8>(reinterpret_cast<u8*>(presentPixels.Data()), presentPixels.Size()));
        if (!packed)
        {
            std::printf("present packing failed\n");
            return 1;
        }

        PresentSource source{};
        source.pixels = Span<const Byte>(presentPixels.Data(), presentPixels.Size());
        source.width = resolved.Width();
        source.height = resolved.Height();
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
