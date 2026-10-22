#pragma once

#include <cyber/core/assert.h>
#include <cyber/platform/types.h>

#include <array>

namespace cyber::platform
{

enum class Key : u16
{
    Unknown = 0,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Escape,
    Enter,
    Tab,
    Space,
    Backspace,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Left,
    Right,
    Up,
    Down,
    Shift,
    Control,
    Alt,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Count,
};

enum class MouseButton : u8
{
    Left = 0,
    Right,
    Middle,
    X1,
    X2,
    Count,
};

class WindowImpl;

// Per-window input snapshot. The window backend mutates it; callers read it.
// BeginFrame clears pressed/released edges, mouse deltas, and wheel delta while
// keeping held states intact.
class InputState
{
public:
    [[nodiscard]] bool IsKeyDown(Key key) const noexcept;
    [[nodiscard]] bool WasKeyPressed(Key key) const noexcept;
    [[nodiscard]] bool WasKeyReleased(Key key) const noexcept;

    [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const noexcept;
    [[nodiscard]] bool WasMouseButtonPressed(MouseButton button) const noexcept;
    [[nodiscard]] bool WasMouseButtonReleased(MouseButton button) const noexcept;

    [[nodiscard]] i32 MouseX() const noexcept
    {
        return m_mouseX;
    }

    [[nodiscard]] i32 MouseY() const noexcept
    {
        return m_mouseY;
    }

    [[nodiscard]] f32 MouseDeltaX() const noexcept
    {
        return m_mouseDeltaX;
    }

    [[nodiscard]] f32 MouseDeltaY() const noexcept
    {
        return m_mouseDeltaY;
    }

    [[nodiscard]] f32 WheelDelta() const noexcept
    {
        return m_wheelDelta;
    }

    void BeginFrame() noexcept;

private:
    friend class WindowImpl;

    struct ButtonState
    {
        bool down = false;
        bool pressed = false;
        bool released = false;
    };

    static constexpr usize kKeyCount = static_cast<usize>(Key::Count);
    static constexpr usize kMouseButtonCount = static_cast<usize>(MouseButton::Count);

    void SetKey(Key key, bool down) noexcept;
    void SetMouseButton(MouseButton button, bool down) noexcept;
    void SetMousePosition(i32 x, i32 y) noexcept;
    void AddMouseDelta(f32 x, f32 y) noexcept;
    void AddWheelDelta(f32 delta) noexcept;
    void ClearKeys() noexcept;
    void Reset() noexcept;

    std::array<ButtonState, kKeyCount> m_keys{};
    std::array<ButtonState, kMouseButtonCount> m_mouseButtons{};
    i32 m_mouseX = 0;
    i32 m_mouseY = 0;
    f32 m_mouseDeltaX = 0.0f;
    f32 m_mouseDeltaY = 0.0f;
    f32 m_wheelDelta = 0.0f;
};

}  // namespace cyber::platform
