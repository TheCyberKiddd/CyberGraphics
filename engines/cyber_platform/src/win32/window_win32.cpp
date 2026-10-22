#include "../window_impl.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cyber/core/error.h>

#include <new>
#include <utility>
#include <windows.h>
#include <windowsx.h>

namespace cyber::platform
{

namespace
{

constexpr wchar_t kWindowClassName[] = L"CyberGraphicsWindow";

core::Error Win32Error(const char* message) noexcept
{
    return core::Error{core::ErrorCode::Internal, message};
}

bool Utf8ToWide(const char* text, wchar_t* out, usize capacity) noexcept
{
    if (text == nullptr || out == nullptr || capacity == 0)
    {
        return false;
    }
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out,
                                            static_cast<int>(capacity));
    return written > 0;
}

Key KeyFromVirtualKey(u32 virtualKey) noexcept
{
    if (virtualKey >= 'A' && virtualKey <= 'Z')
    {
        return static_cast<Key>(static_cast<u16>(Key::A) + static_cast<u16>(virtualKey - 'A'));
    }
    if (virtualKey >= '0' && virtualKey <= '9')
    {
        return static_cast<Key>(static_cast<u16>(Key::Digit0) + static_cast<u16>(virtualKey - '0'));
    }
    if (virtualKey >= VK_F1 && virtualKey <= VK_F12)
    {
        return static_cast<Key>(static_cast<u16>(Key::F1) + static_cast<u16>(virtualKey - VK_F1));
    }

    switch (virtualKey)
    {
    case VK_ESCAPE:
        return Key::Escape;
    case VK_RETURN:
        return Key::Enter;
    case VK_TAB:
        return Key::Tab;
    case VK_SPACE:
        return Key::Space;
    case VK_BACK:
        return Key::Backspace;
    case VK_INSERT:
        return Key::Insert;
    case VK_DELETE:
        return Key::Delete;
    case VK_HOME:
        return Key::Home;
    case VK_END:
        return Key::End;
    case VK_PRIOR:
        return Key::PageUp;
    case VK_NEXT:
        return Key::PageDown;
    case VK_LEFT:
        return Key::Left;
    case VK_RIGHT:
        return Key::Right;
    case VK_UP:
        return Key::Up;
    case VK_DOWN:
        return Key::Down;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return Key::Shift;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return Key::Control;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return Key::Alt;
    default:
        return Key::Unknown;
    }
}

class WindowImplWin32 final : public WindowImpl
{
public:
    explicit WindowImplWin32(memory::Allocator& allocator) noexcept : WindowImpl(allocator) {}

    core::Status<core::Error> Initialize(const WindowDesc& desc) noexcept
    {
        wchar_t title[256] = {};
        if (!Utf8ToWide(desc.title, title, 256))
        {
            return core::Status<core::Error>::Err(Win32Error("window title is not valid UTF-8"));
        }

        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(WNDCLASSEXW);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        windowClass.lpfnWndProc = &WindowImplWin32::WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.lpszClassName = kWindowClassName;

        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return core::Status<core::Error>::Err(Win32Error("RegisterClassExW failed"));
        }

        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        if (desc.resizable)
        {
            style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
        }

        const i32 width = desc.width > 0 ? desc.width : 1;
        const i32 height = desc.height > 0 ? desc.height : 1;
        RECT rect{0, 0, width, height};
        AdjustWindowRectEx(&rect, style, FALSE, 0);

        HWND hwnd = CreateWindowExW(0, kWindowClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                                    rect.right - rect.left, rect.bottom - rect.top, nullptr,
                                    nullptr, instance, this);
        if (hwnd == nullptr)
        {
            return core::Status<core::Error>::Err(Win32Error("CreateWindowExW failed"));
        }

        m_hwnd = hwnd;
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        UpdateClientSize();
        UpdateDpi();

        RAWINPUTDEVICE rawInput{};
        rawInput.usUsagePage = 0x01;
        rawInput.usUsage = 0x02;
        rawInput.dwFlags = RIDEV_INPUTSINK;
        rawInput.hwndTarget = m_hwnd;
        RegisterRawInputDevices(&rawInput, 1, sizeof(RAWINPUTDEVICE));

        m_open = true;

        if (desc.fullscreen)
        {
            core::Status<core::Error> status = SetFullscreen(true);
            if (!status)
            {
                Destroy();
                return status;
            }
        }

        if (desc.visible)
        {
            ShowWindow(m_hwnd, SW_SHOW);
            UpdateWindow(m_hwnd);
        }

        return core::Status<core::Error>::Ok();
    }

    void PollEvents() noexcept override
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0)
        {
            if (message.message == WM_QUIT)
            {
                m_open = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    void WaitEvents() noexcept override
    {
        if (!m_open)
        {
            return;
        }
        MSG message{};
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
        {
            m_open = false;
            return;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    void Resize(i32 width, i32 height) noexcept override
    {
        if (m_hwnd == nullptr || m_fullscreen)
        {
            return;
        }
        const i32 actualWidth = width > 0 ? width : 1;
        const i32 actualHeight = height > 0 ? height : 1;
        const DWORD style = static_cast<DWORD>(GetWindowLongW(m_hwnd, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(m_hwnd, GWL_EXSTYLE));
        RECT rect{0, 0, actualWidth, actualHeight};
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);
        SetWindowPos(m_hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    core::Status<core::Error> SetFullscreen(bool fullscreen) noexcept override
    {
        if (m_hwnd == nullptr)
        {
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Unsupported, "window is not open"});
        }
        if (fullscreen == m_fullscreen)
        {
            return core::Status<core::Error>::Ok();
        }

        if (fullscreen)
        {
            GetWindowRect(m_hwnd, &m_windowedRect);
            m_windowedStyle = static_cast<DWORD>(GetWindowLongW(m_hwnd, GWL_STYLE));
            m_windowedExStyle = static_cast<DWORD>(GetWindowLongW(m_hwnd, GWL_EXSTYLE));

            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(MONITORINFO);
            const HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(monitor, &monitorInfo) == 0)
            {
                return core::Status<core::Error>::Err(Win32Error("GetMonitorInfoW failed"));
            }

            SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
            SetWindowLongW(m_hwnd, GWL_EXSTYLE, m_windowedExStyle & ~WS_EX_TOPMOST);
            SetWindowPos(m_hwnd, HWND_TOP, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                         monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        else
        {
            SetWindowLongW(m_hwnd, GWL_STYLE, static_cast<LONG>(m_windowedStyle));
            SetWindowLongW(m_hwnd, GWL_EXSTYLE, static_cast<LONG>(m_windowedExStyle));
            SetWindowPos(m_hwnd, nullptr, m_windowedRect.left, m_windowedRect.top,
                         m_windowedRect.right - m_windowedRect.left,
                         m_windowedRect.bottom - m_windowedRect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }

        m_fullscreen = fullscreen;
        WindowEvent event{};
        event.type = WindowEventType::FullscreenChanged;
        event.fullscreen = fullscreen;
        PushEvent(event);
        return core::Status<core::Error>::Ok();
    }

    void SetTitle(const char* title) noexcept override
    {
        if (m_hwnd == nullptr || title == nullptr)
        {
            return;
        }
        wchar_t wideTitle[256] = {};
        if (Utf8ToWide(title, wideTitle, 256))
        {
            SetWindowTextW(m_hwnd, wideTitle);
        }
    }

    void RequestClose() noexcept override
    {
        if (m_hwnd != nullptr)
        {
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        }
    }

    void Destroy() noexcept override
    {
        if (m_hwnd != nullptr)
        {
            HWND hwnd = m_hwnd;
            m_hwnd = nullptr;
            DestroyWindow(hwnd);
        }
        m_open = false;
        ResetInput();
    }

    void* NativeHandle() const noexcept override
    {
        return m_hwnd;
    }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        WindowImplWin32* self =
            reinterpret_cast<WindowImplWin32*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<WindowImplWin32*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }

        if (self != nullptr)
        {
            return self->HandleMessage(hwnd, message, wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_NCCREATE:
            m_hwnd = hwnd;
            return TRUE;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            m_open = false;
            m_hwnd = nullptr;
            ResetInput();
            {
                WindowEvent event{};
                event.type = WindowEventType::Close;
                PushEvent(event);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            ValidateRect(hwnd, nullptr);
            return 0;

        case WM_SIZE:
            m_width = static_cast<i32>(LOWORD(lParam));
            m_height = static_cast<i32>(HIWORD(lParam));
            {
                WindowEvent event{};
                event.type = WindowEventType::Resize;
                event.width = m_width;
                event.height = m_height;
                PushEvent(event);
            }
            return 0;

        case WM_DPICHANGED:
            m_dpiScale = static_cast<f32>(HIWORD(wParam)) / 96.0f;
            if (lParam != 0)
            {
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            {
                WindowEvent event{};
                event.type = WindowEventType::DpiChanged;
                event.dpiScale = m_dpiScale;
                PushEvent(event);
            }
            return 0;

        case WM_SETFOCUS:
        {
            WindowEvent event{};
            event.type = WindowEventType::FocusGained;
            PushEvent(event);
        }
            return 0;

        case WM_KILLFOCUS:
            ClearKeys();
            {
                WindowEvent event{};
                event.type = WindowEventType::FocusLost;
                PushEvent(event);
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            const Key key = KeyFromVirtualKey(static_cast<u32>(wParam));
            if (key != Key::Unknown)
            {
                SetKey(key, true);
                WindowEvent event{};
                event.type = WindowEventType::KeyDown;
                event.key = key;
                PushEvent(event);
            }

            const bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (message == WM_SYSKEYDOWN && wParam == VK_RETURN && altDown)
            {
                (void)SetFullscreen(!m_fullscreen);
                return 0;
            }
        }
            return message == WM_SYSKEYDOWN ? DefWindowProcW(hwnd, message, wParam, lParam) : 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            const Key key = KeyFromVirtualKey(static_cast<u32>(wParam));
            if (key != Key::Unknown)
            {
                SetKey(key, false);
                WindowEvent event{};
                event.type = WindowEventType::KeyUp;
                event.key = key;
                PushEvent(event);
            }
        }
            return 0;

        case WM_CHAR:
            if (wParam >= 32 && wParam != 127)
            {
                WindowEvent event{};
                event.type = WindowEventType::TextInput;
                event.codepoint = static_cast<u32>(wParam);
                PushEvent(event);
            }
            return 0;

        case WM_MOUSEMOVE:
        {
            const i32 x = GET_X_LPARAM(lParam);
            const i32 y = GET_Y_LPARAM(lParam);
            SetMousePosition(x, y);
            WindowEvent event{};
            event.type = WindowEventType::MouseMove;
            event.x = x;
            event.y = y;
            PushEvent(event);
        }
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        {
            MouseButton button = MouseButton::Left;
            bool down = false;
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
            {
                button = MouseButton::Left;
                down = message == WM_LBUTTONDOWN;
            }
            else if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
            {
                button = MouseButton::Right;
                down = message == WM_RBUTTONDOWN;
            }
            else
            {
                button = MouseButton::Middle;
                down = message == WM_MBUTTONDOWN;
            }

            SetMouseButton(button, down);
            if (down)
            {
                SetCapture(hwnd);
            }
            else if (!(GetKeyState(VK_LBUTTON) & 0x8000) && !(GetKeyState(VK_RBUTTON) & 0x8000) &&
                     !(GetKeyState(VK_MBUTTON) & 0x8000))
            {
                ReleaseCapture();
            }

            WindowEvent event{};
            event.type = down ? WindowEventType::MouseButtonDown : WindowEventType::MouseButtonUp;
            event.button = button;
            event.x = GET_X_LPARAM(lParam);
            event.y = GET_Y_LPARAM(lParam);
            PushEvent(event);
        }
            return 0;

        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        {
            const bool down = message == WM_XBUTTONDOWN;
            const MouseButton button =
                GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
            SetMouseButton(button, down);
            WindowEvent event{};
            event.type = down ? WindowEventType::MouseButtonDown : WindowEventType::MouseButtonUp;
            event.button = button;
            event.x = GET_X_LPARAM(lParam);
            event.y = GET_Y_LPARAM(lParam);
            PushEvent(event);
        }
            return TRUE;

        case WM_MOUSEWHEEL:
        {
            const f32 delta =
                static_cast<f32>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<f32>(WHEEL_DELTA);
            AddWheelDelta(delta);
            WindowEvent event{};
            event.type = WindowEventType::MouseWheel;
            event.wheel = delta;
            event.x = GET_X_LPARAM(lParam);
            event.y = GET_Y_LPARAM(lParam);
            PushEvent(event);
        }
            return 0;

        case WM_INPUT:
            HandleRawInput(lParam);
            return DefWindowProcW(hwnd, message, wParam, lParam);

        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void HandleRawInput(LPARAM lParam) noexcept
    {
        UINT size = 0;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size,
                            sizeof(RAWINPUTHEADER)) != 0)
        {
            return;
        }
        if (size == 0 || size > sizeof(RAWINPUT))
        {
            return;
        }

        RAWINPUT raw{};
        UINT read = size;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &read,
                            sizeof(RAWINPUTHEADER)) != size)
        {
            return;
        }
        if (raw.header.dwType != RIM_TYPEMOUSE)
        {
            return;
        }

        const RAWMOUSE& mouse = raw.data.mouse;
        if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
        {
            AddMouseDelta(static_cast<f32>(mouse.lLastX), static_cast<f32>(mouse.lLastY));
        }
    }

    void UpdateClientSize() noexcept
    {
        RECT client{};
        if (m_hwnd != nullptr && GetClientRect(m_hwnd, &client) != 0)
        {
            m_width = client.right - client.left;
            m_height = client.bottom - client.top;
        }
    }

    void UpdateDpi() noexcept
    {
        if (m_hwnd != nullptr)
        {
            const UINT dpi = GetDpiForWindow(m_hwnd);
            m_dpiScale = static_cast<f32>(dpi == 0 ? 96 : dpi) / 96.0f;
        }
    }

    HWND m_hwnd = nullptr;
    RECT m_windowedRect{};
    DWORD m_windowedStyle = 0;
    DWORD m_windowedExStyle = 0;
};

}  // namespace

core::Result<WindowImpl*, core::Error> CreateWindowImpl(const WindowDesc& desc,
                                                        memory::Allocator& allocator) noexcept
{
    void* memory = allocator.Allocate(sizeof(WindowImplWin32), alignof(WindowImplWin32));
    if (memory == nullptr)
    {
        return core::Result<WindowImpl*, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate the window implementation"});
    }

    auto* impl = static_cast<WindowImplWin32*>(memory);
    std::construct_at(impl, allocator);
    core::Status<core::Error> status = impl->Initialize(desc);
    if (!status)
    {
        impl->Destroy();
        impl->~WindowImplWin32();
        allocator.Deallocate(memory);
        return core::Result<WindowImpl*, core::Error>::Err(status.Error());
    }
    return core::Result<WindowImpl*, core::Error>::Ok(static_cast<WindowImpl*>(impl));
}

bool IsWindowSystemSupported() noexcept
{
    return true;
}

}  // namespace cyber::platform
