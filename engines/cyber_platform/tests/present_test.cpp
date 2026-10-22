#include <cyber/core/error.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/present.h>
#include <cyber/platform/window.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::platform;
using namespace cyber::memory;

namespace
{

PresentSource MakeSource(u8* pixels, usize sizeBytes, i32 width, i32 height, usize strideBytes)
{
    PresentSource source{};
    source.pixels = Span<const Byte>(reinterpret_cast<const Byte*>(pixels), sizeBytes);
    source.width = width;
    source.height = height;
    source.strideBytes = strideBytes;
    return source;
}

bool FailsWith(const cyber::core::Status<cyber::core::Error>& status, cyber::core::ErrorCode code)
{
    return !status && status.Error().code == code;
}

}  // namespace

CG_TEST(platform_present_default_state)
{
    Presenter presenter;
    CG_EXPECT_FALSE(presenter.IsValid());

    PresentSource source{};
    CG_EXPECT_TRUE(FailsWith(presenter.Present(source), cyber::core::ErrorCode::Unsupported));
}

CG_TEST(platform_present_create_requires_window)
{
    HeapAllocator heap;
    Window window;
    auto created = Presenter::Create(window, heap);
    CG_EXPECT_FALSE(static_cast<bool>(created));
    if (!created)
    {
        CG_EXPECT_TRUE(created.Error().code == cyber::core::ErrorCode::Unsupported);
    }
}

CG_TEST(platform_present_invalid_sources)
{
    HeapAllocator heap;
    WindowDesc desc{};
    desc.title = "CyberGraphics present test";
    desc.width = 64;
    desc.height = 64;
    desc.visible = false;
    desc.resizable = true;

    auto createdWindow = Window::Create(desc, heap);
    if (!IsWindowSystemSupported())
    {
        CG_EXPECT_FALSE(static_cast<bool>(createdWindow));
        return;
    }
    CG_REQUIRE(static_cast<bool>(createdWindow));
    Window window = std::move(createdWindow.Value());

    auto createdPresenter = Presenter::Create(window, heap);
    CG_REQUIRE(static_cast<bool>(createdPresenter));
    Presenter presenter = std::move(createdPresenter.Value());
    CG_EXPECT_TRUE(presenter.IsValid());

    u8 pixels[8 * 8 * 4] = {};

    PresentSource empty{};
    CG_EXPECT_TRUE(FailsWith(presenter.Present(empty), cyber::core::ErrorCode::InvalidArgument));

    PresentSource zeroWidth = MakeSource(pixels, sizeof(pixels), 0, 8, 0);
    CG_EXPECT_TRUE(
        FailsWith(presenter.Present(zeroWidth), cyber::core::ErrorCode::InvalidArgument));

    PresentSource negativeHeight = MakeSource(pixels, sizeof(pixels), 8, -1, 0);
    CG_EXPECT_TRUE(
        FailsWith(presenter.Present(negativeHeight), cyber::core::ErrorCode::InvalidArgument));

    PresentSource smallStride = MakeSource(pixels, sizeof(pixels), 8, 8, 16);
    CG_EXPECT_TRUE(
        FailsWith(presenter.Present(smallStride), cyber::core::ErrorCode::InvalidArgument));

    PresentSource smallSpan = MakeSource(pixels, 8 * 8 * 4 - 1, 8, 8, 0);
    CG_EXPECT_TRUE(
        FailsWith(presenter.Present(smallSpan), cyber::core::ErrorCode::InvalidArgument));

    PresentSource unknownFormat = MakeSource(pixels, sizeof(pixels), 8, 8, 0);
    unknownFormat.format = static_cast<PresentFormat>(9);
    CG_EXPECT_TRUE(
        FailsWith(presenter.Present(unknownFormat), cyber::core::ErrorCode::InvalidArgument));
}

CG_TEST(platform_present_hidden_window)
{
    HeapAllocator heap;
    WindowDesc desc{};
    desc.title = "CyberGraphics present test";
    desc.width = 64;
    desc.height = 64;
    desc.visible = false;
    desc.resizable = true;

    auto createdWindow = Window::Create(desc, heap);
    if (!IsWindowSystemSupported())
    {
        CG_EXPECT_FALSE(static_cast<bool>(createdWindow));
        return;
    }
    CG_REQUIRE(static_cast<bool>(createdWindow));
    Window window = std::move(createdWindow.Value());

    auto createdPresenter = Presenter::Create(window, heap);
    CG_REQUIRE(static_cast<bool>(createdPresenter));
    Presenter presenter = std::move(createdPresenter.Value());

    u8 pixels[8 * 8 * 4] = {};
    for (usize index = 0; index < sizeof(pixels); ++index)
    {
        pixels[index] = static_cast<u8>(index & 0xFF);
    }
    PresentSource source = MakeSource(pixels, sizeof(pixels), 8, 8, 0);
    CG_EXPECT_TRUE(static_cast<bool>(presenter.Present(source)));

    window.Resize(96, 48);
    window.PollEvents();
    CG_EXPECT_TRUE(static_cast<bool>(presenter.Present(source)));

    // A padded stride is accepted as long as every row fits the span.
    u8 padded[4 * 32] = {};
    PresentSource paddedSource = MakeSource(padded, sizeof(padded), 4, 4, 32);
    CG_EXPECT_TRUE(static_cast<bool>(presenter.Present(paddedSource)));

    Presenter moved = std::move(presenter);
    CG_EXPECT_FALSE(presenter.IsValid());
    CG_EXPECT_TRUE(moved.IsValid());
    CG_EXPECT_TRUE(static_cast<bool>(moved.Present(source)));
    CG_EXPECT_TRUE(FailsWith(presenter.Present(source), cyber::core::ErrorCode::Unsupported));

    window.RequestClose();
    window.PollEvents();
    CG_EXPECT_TRUE(FailsWith(moved.Present(source), cyber::core::ErrorCode::Unsupported));
}
