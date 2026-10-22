#pragma once

#include <cyber/platform/window.h>

#include <array>

namespace cyber::platform
{

// Shared window state and event queue. Platform backends implement the virtual
// OS operations; the public Window delegates here.
class WindowImpl
{
public:
    static constexpr usize kEventCapacity = 256;

    explicit WindowImpl(memory::Allocator& allocator) noexcept : allocator(&allocator) {}
    virtual ~WindowImpl() = default;

    WindowImpl(const WindowImpl&) = delete;
    WindowImpl& operator=(const WindowImpl&) = delete;

    virtual void PollEvents() noexcept = 0;
    virtual void WaitEvents() noexcept = 0;
    virtual void Resize(i32 width, i32 height) noexcept = 0;
    virtual core::Status<core::Error> SetFullscreen(bool fullscreen) noexcept = 0;
    virtual void SetTitle(const char* title) noexcept = 0;
    virtual void RequestClose() noexcept = 0;
    virtual void Destroy() noexcept = 0;
    virtual void* NativeHandle() const noexcept = 0;

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return m_open;
    }

    [[nodiscard]] bool PollEvent(WindowEvent& event) noexcept
    {
        if (m_eventCount == 0)
        {
            return false;
        }
        event = m_events[m_eventHead];
        m_eventHead = (m_eventHead + 1U) % kEventCapacity;
        --m_eventCount;
        return true;
    }

    void BeginFrame() noexcept
    {
        m_input.BeginFrame();
    }

    [[nodiscard]] i32 Width() const noexcept
    {
        return m_width;
    }

    [[nodiscard]] i32 Height() const noexcept
    {
        return m_height;
    }

    [[nodiscard]] f32 DpiScale() const noexcept
    {
        return m_dpiScale;
    }

    [[nodiscard]] bool IsFullscreen() const noexcept
    {
        return m_fullscreen;
    }

    [[nodiscard]] const InputState& Input() const noexcept
    {
        return m_input;
    }

    memory::Allocator* allocator = nullptr;

protected:
    void PushEvent(const WindowEvent& event) noexcept
    {
        if (m_eventCount == kEventCapacity)
        {
            m_eventHead = (m_eventHead + 1U) % kEventCapacity;
            --m_eventCount;
        }
        m_events[(m_eventHead + m_eventCount) % kEventCapacity] = event;
        ++m_eventCount;
    }

    void SetKey(Key key, bool down) noexcept
    {
        m_input.SetKey(key, down);
    }

    void SetMouseButton(MouseButton button, bool down) noexcept
    {
        m_input.SetMouseButton(button, down);
    }

    void SetMousePosition(i32 x, i32 y) noexcept
    {
        m_input.SetMousePosition(x, y);
    }

    void AddMouseDelta(f32 x, f32 y) noexcept
    {
        m_input.AddMouseDelta(x, y);
    }

    void AddWheelDelta(f32 delta) noexcept
    {
        m_input.AddWheelDelta(delta);
    }

    void ClearKeys() noexcept
    {
        m_input.ClearKeys();
    }

    void ResetInput() noexcept
    {
        m_input.Reset();
    }

    InputState m_input{};
    i32 m_width = 0;
    i32 m_height = 0;
    f32 m_dpiScale = 1.0f;
    bool m_open = false;
    bool m_fullscreen = false;

private:
    std::array<WindowEvent, kEventCapacity> m_events{};
    usize m_eventHead = 0;
    usize m_eventCount = 0;
};

[[nodiscard]] core::Result<WindowImpl*, core::Error>
CreateWindowImpl(const WindowDesc& desc, memory::Allocator& allocator) noexcept;

}  // namespace cyber::platform
