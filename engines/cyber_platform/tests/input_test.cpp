#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/input.h>
#include <cyber/platform/window.h>
#include <cyber/test/test.h>

#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace cyber::platform;
using namespace cyber::memory;

CG_TEST(platform_input_default_state)
{
    const InputState state;
    CG_EXPECT_FALSE(state.IsKeyDown(Key::A));
    CG_EXPECT_FALSE(state.WasKeyPressed(Key::A));
    CG_EXPECT_FALSE(state.WasKeyReleased(Key::A));
    CG_EXPECT_FALSE(state.IsMouseButtonDown(MouseButton::Left));
    CG_EXPECT_FALSE(state.WasMouseButtonPressed(MouseButton::Left));
    CG_EXPECT_FALSE(state.WasMouseButtonReleased(MouseButton::Left));
    CG_EXPECT_EQ(state.MouseX(), i32{0});
    CG_EXPECT_EQ(state.MouseY(), i32{0});
    CG_EXPECT_EQ(state.MouseDeltaX(), 0.0f);
    CG_EXPECT_EQ(state.MouseDeltaY(), 0.0f);
    CG_EXPECT_EQ(state.WheelDelta(), 0.0f);

    // Out-of-range keys and buttons are safe and report false.
    CG_EXPECT_FALSE(state.IsKeyDown(Key::Count));
    CG_EXPECT_FALSE(state.IsKeyDown(Key::Unknown));
    CG_EXPECT_FALSE(state.WasKeyPressed(Key::Count));
    CG_EXPECT_FALSE(state.IsMouseButtonDown(MouseButton::Count));
    CG_EXPECT_FALSE(state.WasMouseButtonPressed(MouseButton::Count));
}

CG_TEST(platform_input_begin_frame_is_safe)
{
    InputState state;
    state.BeginFrame();
    state.BeginFrame();
    CG_EXPECT_EQ(state.MouseDeltaX(), 0.0f);
    CG_EXPECT_EQ(state.WheelDelta(), 0.0f);
    CG_EXPECT_FALSE(state.IsKeyDown(Key::Space));
}

#if defined(_WIN32)

CG_TEST(platform_input_from_window_messages)
{
    HeapAllocator heap;
    WindowDesc desc{};
    desc.title = "CyberGraphics input test";
    desc.width = 200;
    desc.height = 150;
    desc.visible = false;
    desc.resizable = false;

    auto created = Window::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Window window = std::move(created.Value());
    HWND hwnd = static_cast<HWND>(window.NativeHandle());
    CG_REQUIRE(hwnd != nullptr);

    PostMessageW(hwnd, WM_KEYDOWN, 'W', 0);
    window.PollEvents();
    CG_EXPECT_TRUE(window.Input().IsKeyDown(Key::W));
    CG_EXPECT_TRUE(window.Input().WasKeyPressed(Key::W));

    // BeginFrame clears the pressed edge but keeps the held state.
    window.BeginFrame();
    CG_EXPECT_TRUE(window.Input().IsKeyDown(Key::W));
    CG_EXPECT_FALSE(window.Input().WasKeyPressed(Key::W));

    PostMessageW(hwnd, WM_KEYUP, 'W', 0);
    window.PollEvents();
    CG_EXPECT_FALSE(window.Input().IsKeyDown(Key::W));
    CG_EXPECT_TRUE(window.Input().WasKeyReleased(Key::W));

    PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(11, 22));
    window.PollEvents();
    CG_EXPECT_EQ(window.Input().MouseX(), i32{11});
    CG_EXPECT_EQ(window.Input().MouseY(), i32{22});

    PostMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(0, 0));
    window.PollEvents();
    CG_EXPECT_TRUE(window.Input().WheelDelta() > 0.0f);
    window.BeginFrame();
    CG_EXPECT_EQ(window.Input().WheelDelta(), 0.0f);

    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 6));
    window.PollEvents();
    CG_EXPECT_TRUE(window.Input().IsMouseButtonDown(MouseButton::Left));
    CG_EXPECT_TRUE(window.Input().WasMouseButtonPressed(MouseButton::Left));

    PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(5, 6));
    window.PollEvents();
    CG_EXPECT_FALSE(window.Input().IsMouseButtonDown(MouseButton::Left));
    CG_EXPECT_TRUE(window.Input().WasMouseButtonReleased(MouseButton::Left));

    // Losing focus releases every held key.
    PostMessageW(hwnd, WM_KEYDOWN, 'A', 0);
    window.PollEvents();
    CG_EXPECT_TRUE(window.Input().IsKeyDown(Key::A));
    PostMessageW(hwnd, WM_KILLFOCUS, 0, 0);
    window.PollEvents();
    CG_EXPECT_FALSE(window.Input().IsKeyDown(Key::A));
    CG_EXPECT_TRUE(window.Input().WasKeyReleased(Key::A));
}

#endif  // _WIN32
