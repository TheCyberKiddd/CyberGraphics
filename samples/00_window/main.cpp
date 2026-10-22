// CyberGraphics Phase 5 visible-window sample.
//
// Proves the platform layer can open a window, pump events, react to resize and
// fullscreen changes, and read input without linking the renderer or the RHI.
// Run it on Windows for a visible window; the null backend prints a message.

#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/platform.h>

#include <cstdio>
#include <utility>

using namespace cyber::platform;
using namespace cyber::memory;

namespace
{

void PrintEvent(const WindowEvent& event)
{
    switch (event.type)
    {
    case WindowEventType::Resize:
        std::printf("resize %d x %d\n", event.width, event.height);
        break;
    case WindowEventType::FullscreenChanged:
        std::printf("fullscreen %s\n", event.fullscreen ? "on" : "off");
        break;
    case WindowEventType::DpiChanged:
        std::printf("dpi scale %.3f\n", static_cast<double>(event.dpiScale));
        break;
    default:
        break;
    }
}

}  // namespace

int main()
{
    if (!IsWindowSystemSupported())
    {
        std::printf("CyberGraphics window sample: no window backend on this platform.\n");
        return 0;
    }

    HeapAllocator heap;
    WindowDesc desc{};
    desc.title = "CyberGraphics window sample";
    desc.width = 1280;
    desc.height = 720;
    desc.resizable = true;
    desc.visible = true;

    auto created = Window::Create(desc, heap);
    if (!created)
    {
        std::printf("window creation failed: %s\n",
                    cyber::core::ErrorCodeName(created.Error().code));
        return 1;
    }
    Window window = std::move(created.Value());

    std::printf("controls: F11 toggles fullscreen, Escape closes the window\n");

    FrameTimer timer;
    u64 frames = 0;
    while (window.IsOpen())
    {
        window.BeginFrame();
        window.PollEvents();

        WindowEvent event{};
        while (window.PollEvent(event))
        {
            PrintEvent(event);
            if (event.type == WindowEventType::KeyDown)
            {
                if (event.key == Key::Escape)
                {
                    window.RequestClose();
                }
                else if (event.key == Key::F11)
                {
                    (void)window.SetFullscreen(!window.IsFullscreen());
                }
            }
        }

        const f64 delta = timer.Tick();
        (void)delta;
        ++frames;
    }

    std::printf("window closed after %llu frames\n", static_cast<unsigned long long>(frames));
    return 0;
}
