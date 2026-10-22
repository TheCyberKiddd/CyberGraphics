#include <cyber/platform/input.h>

namespace cyber::platform
{

namespace
{

[[nodiscard]] bool IsValidKey(Key key) noexcept
{
    return key != Key::Count && static_cast<usize>(key) < static_cast<usize>(Key::Count);
}

[[nodiscard]] bool IsValidMouseButton(MouseButton button) noexcept
{
    return static_cast<usize>(button) < static_cast<usize>(MouseButton::Count);
}

}  // namespace

bool InputState::IsKeyDown(Key key) const noexcept
{
    return IsValidKey(key) && m_keys[static_cast<usize>(key)].down;
}

bool InputState::WasKeyPressed(Key key) const noexcept
{
    return IsValidKey(key) && m_keys[static_cast<usize>(key)].pressed;
}

bool InputState::WasKeyReleased(Key key) const noexcept
{
    return IsValidKey(key) && m_keys[static_cast<usize>(key)].released;
}

bool InputState::IsMouseButtonDown(MouseButton button) const noexcept
{
    return IsValidMouseButton(button) && m_mouseButtons[static_cast<usize>(button)].down;
}

bool InputState::WasMouseButtonPressed(MouseButton button) const noexcept
{
    return IsValidMouseButton(button) && m_mouseButtons[static_cast<usize>(button)].pressed;
}

bool InputState::WasMouseButtonReleased(MouseButton button) const noexcept
{
    return IsValidMouseButton(button) && m_mouseButtons[static_cast<usize>(button)].released;
}

void InputState::BeginFrame() noexcept
{
    for (ButtonState& state : m_keys)
    {
        state.pressed = false;
        state.released = false;
    }
    for (ButtonState& state : m_mouseButtons)
    {
        state.pressed = false;
        state.released = false;
    }
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_wheelDelta = 0.0f;
}

void InputState::SetKey(Key key, bool down) noexcept
{
    if (!IsValidKey(key))
    {
        return;
    }
    ButtonState& state = m_keys[static_cast<usize>(key)];
    if (down && !state.down)
    {
        state.pressed = true;
    }
    if (!down && state.down)
    {
        state.released = true;
    }
    state.down = down;
}

void InputState::SetMouseButton(MouseButton button, bool down) noexcept
{
    if (!IsValidMouseButton(button))
    {
        return;
    }
    ButtonState& state = m_mouseButtons[static_cast<usize>(button)];
    if (down && !state.down)
    {
        state.pressed = true;
    }
    if (!down && state.down)
    {
        state.released = true;
    }
    state.down = down;
}

void InputState::SetMousePosition(i32 x, i32 y) noexcept
{
    m_mouseX = x;
    m_mouseY = y;
}

void InputState::AddMouseDelta(f32 x, f32 y) noexcept
{
    m_mouseDeltaX += x;
    m_mouseDeltaY += y;
}

void InputState::AddWheelDelta(f32 delta) noexcept
{
    m_wheelDelta += delta;
}

void InputState::ClearKeys() noexcept
{
    for (ButtonState& state : m_keys)
    {
        if (state.down)
        {
            state.down = false;
            state.released = true;
        }
    }
    for (ButtonState& state : m_mouseButtons)
    {
        if (state.down)
        {
            state.down = false;
            state.released = true;
        }
    }
}

void InputState::Reset() noexcept
{
    m_keys = {};
    m_mouseButtons = {};
    m_mouseX = 0;
    m_mouseY = 0;
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_wheelDelta = 0.0f;
}

}  // namespace cyber::platform
