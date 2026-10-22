#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/allocator.h>
#include <cyber/platform/input.h>
#include <cyber/platform/types.h>

namespace cyber::platform
{

class WindowImpl;

struct WindowDesc
{
    const char* title = "CyberGraphics";
    i32 width = 1280;
    i32 height = 720;
    bool resizable = true;
    bool fullscreen = false;
    bool visible = true;
};

enum class WindowEventType : u8
{
    None = 0,
    Close,
    Resize,
    FocusGained,
    FocusLost,
    KeyDown,
    KeyUp,
    TextInput,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    DpiChanged,
    FullscreenChanged,
};

struct WindowEvent
{
    WindowEventType type = WindowEventType::None;
    Key key = Key::Unknown;
    MouseButton button = MouseButton::Left;
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;
    f32 wheel = 0.0f;
    f32 dpiScale = 1.0f;
    u32 codepoint = 0;
    bool fullscreen = false;
};

// A single OS window. Windows are move-only, own their native resources, and are
// allocated from a caller-supplied allocator. Unsupported platforms return
// ErrorCode::Unsupported from Create.
class Window
{
public:
    Window() noexcept = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    [[nodiscard]] static core::Result<Window, core::Error>
    Create(const WindowDesc& desc, memory::Allocator& allocator) noexcept;

    // Pumps all pending OS messages into the window event queue.
    void PollEvents() noexcept;

    // Pops one queued event without pumping the OS queue. Returns false when the
    // queue is empty.
    [[nodiscard]] bool PollEvent(WindowEvent& event) noexcept;

    // Blocks for exactly one OS message and dispatches it.
    void WaitEvents() noexcept;

    // Clears per-frame input edges and mouse deltas. Call once per frame before
    // PollEvents.
    void BeginFrame() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept;
    void RequestClose() noexcept;

    void Resize(i32 width, i32 height) noexcept;
    [[nodiscard]] core::Status<core::Error> SetFullscreen(bool fullscreen) noexcept;
    [[nodiscard]] bool IsFullscreen() const noexcept;

    void SetTitle(const char* title) noexcept;

    [[nodiscard]] i32 Width() const noexcept;
    [[nodiscard]] i32 Height() const noexcept;
    [[nodiscard]] f32 DpiScale() const noexcept;

    [[nodiscard]] const InputState& Input() const noexcept;

    // Opaque native window handle. Windows returns HWND; the null backend returns
    // nullptr. The handle remains owned by the Window.
    [[nodiscard]] void* NativeHandle() const noexcept;

private:
    explicit Window(WindowImpl* impl) noexcept;

    WindowImpl* m_impl = nullptr;
};

// False when the build has no usable window backend (for example Linux in CI).
[[nodiscard]] bool IsWindowSystemSupported() noexcept;

}  // namespace cyber::platform
