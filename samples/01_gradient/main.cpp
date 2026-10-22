// CyberGraphics Phase 6 visible gradient sample.
//
// Renders a linear-space animated gradient with a moving checkerboard, packs it
// to sRGB RGBA8, and blits it into a window through cyber_platform::Presenter.
// A 5x7 bitmap overlay shows the smoothed frame time and frame rate. The frame
// loop is paced by software::FramePacer at 60 Hz.
//
// Controls: Escape closes, F11 toggles fullscreen, F2 toggles the overlay.
// `--capture <path>` renders one fixed frame offscreen, writes it twice, and
// fails unless the two files are byte-identical. Capture mode needs no window.

#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/platform/platform.h>
#include <cyber/software/software.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

using namespace cyber::platform;
using namespace cyber::software;
using namespace cyber::memory;
using cyber::math::Color;

namespace
{

constexpr i32 kDefaultWidth = 1280;
constexpr i32 kDefaultHeight = 720;
constexpr i32 kCheckerSize = 24;
constexpr usize kCapturePathBytes = 512;

struct Options
{
    const char* capturePath = nullptr;
};

void PrintUsage()
{
    std::printf("usage: cyber_sample_gradient [--capture <path.ppm|path.tga>]\n");
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

// Same deterministic scene as the screenshot test: two-axis linear gradient,
// a checkerboard that moves one cell every 250 ms, and an overlay line.
void RenderScene(Framebuffer& target, f32 seconds, bool drawOverlay, f32 frameMilliseconds,
                 f32 framesPerSecond)
{
    const i32 width = target.Width();
    const i32 height = target.Height();
    const i32 checkerShift = static_cast<i32>(seconds * 4.0f);
    const f32 widthDenominator = static_cast<f32>(width > 1 ? width - 1 : 1);
    const f32 heightDenominator = static_cast<f32>(height > 1 ? height - 1 : 1);

    for (i32 y = 0; y < height; ++y)
    {
        const f32 v = static_cast<f32>(y) / heightDenominator;
        for (i32 x = 0; x < width; ++x)
        {
            const f32 u = static_cast<f32>(x) / widthDenominator;
            const bool dark = (((x / kCheckerSize) + (y / kCheckerSize) + checkerShift) & 1) != 0;
            const f32 checker = dark ? 0.18f : 0.82f;
            const f32 pulse = 0.5f + 0.5f * std::sin(seconds * 2.0f);
            target.SetPixel(x, y,
                            Color{checker * u, checker * v,
                                  checker * (0.35f + 0.3f * pulse + 0.2f * (u + v)), 1.0f});
        }
    }

    if (!drawOverlay)
    {
        return;
    }

    char text[64] = {};
    std::snprintf(text, sizeof(text), "%.2f MS  %.1f FPS", static_cast<double>(frameMilliseconds),
                  static_cast<double>(framesPerSecond));

    const TextMetrics metrics = MeasureText(text, 2);
    target.ClearRect(4, 4, metrics.width + 16, metrics.height + 12, Color{0.02f, 0.02f, 0.02f});
    DrawText(target, 10, 10, text, Color{1.0f, 1.0f, 1.0f}, 2);

    // Static caption so a screenshot is self-describing.
    DrawText(target, 10, 10 + metrics.height + 8, "LINEAR GRADIENT  CHECKER",
             Color{0.7f, 0.8f, 1.0f});
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

// Builds "<stem>.copy<extension>" next to the requested path.
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
    RenderScene(framebuffer, 1.25f, true, 16.67f, 60.0f);

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

int RunWindow(Allocator& allocator)
{
    if (!IsWindowSystemSupported())
    {
        std::printf("CyberGraphics gradient sample: no window backend on this platform.\n");
        std::printf("Use --capture <path> to render a frame offscreen.\n");
        return 0;
    }

    WindowDesc windowDesc{};
    windowDesc.title = "CyberGraphics Phase 6 - gradient and present";
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
    if (!presentPixels.TryResize(presentSize))
    {
        std::printf("present buffer allocation failed\n");
        return 1;
    }

    PrintUsage();

    FramePacer pacer(60);
    bool overlay = true;
    bool running = true;
    f32 animationSeconds = 0.0f;

    while (running && window.IsOpen())
    {
        const f64 frameDelta = pacer.Wait();
        animationSeconds += static_cast<f32>(frameDelta > 0.1 ? 0.1 : frameDelta);

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

        RenderScene(framebuffer, animationSeconds, overlay,
                    static_cast<f32>(pacer.SmoothedFrameSeconds() * 1000.0),
                    static_cast<f32>(pacer.SmoothedFramesPerSecond()));

        auto packed =
            PackPresentPixels(framebuffer, Span<u8>(reinterpret_cast<u8*>(presentPixels.Data()),
                                                    presentPixels.Size()));
        if (!packed)
        {
            std::printf("present packing failed: %s\n",
                        cyber::core::ErrorCodeName(packed.Error().code));
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
            // A window closed mid-frame is not an error for the sample.
            if (!window.IsOpen())
            {
                break;
            }
            std::printf("present failed: %s\n", cyber::core::ErrorCodeName(presented.Error().code));
            return 1;
        }
    }

    std::printf("gradient sample closed after %llu frames, smoothed %.2f ms (%.1f FPS)\n",
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
        return 1;
    }

    HeapAllocator heap;
    if (options.capturePath != nullptr)
    {
        return RunCapture(options.capturePath, heap);
    }
    return RunWindow(heap);
}
