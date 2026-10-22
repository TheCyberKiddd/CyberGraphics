#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/window.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::platform;
using namespace cyber::memory;

CG_TEST(platform_window_default_state)
{
    Window window;
    CG_EXPECT_FALSE(window.IsOpen());
    CG_EXPECT_EQ(window.Width(), i32{0});
    CG_EXPECT_EQ(window.Height(), i32{0});
    CG_EXPECT_EQ(window.DpiScale(), 1.0f);
    CG_EXPECT_TRUE(window.NativeHandle() == nullptr);
    CG_EXPECT_FALSE(window.Input().IsKeyDown(Key::Escape));
    CG_EXPECT_FALSE(static_cast<bool>(window.SetFullscreen(false)));
}

CG_TEST(platform_window_create_and_close)
{
    HeapAllocator heap;
    WindowDesc desc{};
    desc.title = "CyberGraphics platform test";
    desc.width = 320;
    desc.height = 240;
    desc.visible = false;
    desc.resizable = true;

    auto created = Window::Create(desc, heap);
    if (!IsWindowSystemSupported())
    {
        CG_EXPECT_FALSE(static_cast<bool>(created));
        if (!created)
        {
            CG_EXPECT_TRUE(created.Error().code == cyber::core::ErrorCode::Unsupported);
        }
        return;
    }

    CG_REQUIRE(static_cast<bool>(created));
    Window window = std::move(created.Value());
    CG_EXPECT_TRUE(window.IsOpen());
    CG_EXPECT_TRUE(window.Width() > 0);
    CG_EXPECT_TRUE(window.Height() > 0);
    CG_EXPECT_TRUE(window.DpiScale() > 0.0f);
    CG_EXPECT_TRUE(window.NativeHandle() != nullptr);
    CG_EXPECT_FALSE(window.IsFullscreen());

    window.SetTitle("renamed");
    window.Resize(400, 300);
    window.PollEvents();

    bool sawResize = false;
    WindowEvent event{};
    while (window.PollEvent(event))
    {
        if (event.type == WindowEventType::Resize && event.width > 0 && event.height > 0)
        {
            sawResize = true;
        }
    }
    CG_EXPECT_TRUE(sawResize);

    auto enteredFullscreen = window.SetFullscreen(true);
    CG_REQUIRE(static_cast<bool>(enteredFullscreen));
    CG_EXPECT_TRUE(window.IsFullscreen());
    window.PollEvents();

    bool sawFullscreenOn = false;
    while (window.PollEvent(event))
    {
        if (event.type == WindowEventType::FullscreenChanged && event.fullscreen)
        {
            sawFullscreenOn = true;
        }
    }
    CG_EXPECT_TRUE(sawFullscreenOn);

    auto leftFullscreen = window.SetFullscreen(false);
    CG_REQUIRE(static_cast<bool>(leftFullscreen));
    CG_EXPECT_FALSE(window.IsFullscreen());
    window.PollEvents();

    bool sawFullscreenOff = false;
    while (window.PollEvent(event))
    {
        if (event.type == WindowEventType::FullscreenChanged && !event.fullscreen)
        {
            sawFullscreenOff = true;
        }
    }
    CG_EXPECT_TRUE(sawFullscreenOff);

    Window moved = std::move(window);
    CG_EXPECT_FALSE(window.IsOpen());
    CG_EXPECT_TRUE(moved.IsOpen());

    moved.RequestClose();
    moved.PollEvents();
    CG_EXPECT_FALSE(moved.IsOpen());

    bool sawClose = false;
    while (moved.PollEvent(event))
    {
        if (event.type == WindowEventType::Close)
        {
            sawClose = true;
        }
    }
    CG_EXPECT_TRUE(sawClose);
}
