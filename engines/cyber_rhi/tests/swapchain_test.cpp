#include "rhi_fixture.h"

#include <cyber/rhi/swapchain.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using namespace cyber::rhi::test;
using cyber::core::ErrorCode;
using cyber::core::u32;

namespace
{

// The null backend never dereferences the native handle, but it must not be
// null: the RHI rejects a surface without one.
struct FakeSurface
{
    int token = 0;
};

SwapChainDesc MakeDesc(const FakeSurface& surface) noexcept
{
    SwapChainDesc desc{};
    desc.surface.nativeWindow = &surface.token;
    desc.surface.width = 320;
    desc.surface.height = 200;
    desc.format = Format::BGRA8UnormSrgb;
    desc.imageCount = 2;
    desc.presentMode = PresentMode::Fifo;
    desc.label = "test swapchain";
    return desc;
}

}  // namespace

CG_TEST(rhi_swapchain_description_rules)
{
    FakeSurface surface;
    SwapChainDesc desc = MakeDesc(surface);
    CG_EXPECT_TRUE(ValidateSwapChainDesc(desc).IsOk());

    SwapChainDesc noWindow = desc;
    noWindow.surface.nativeWindow = nullptr;
    CG_EXPECT_FALSE(ValidateSwapChainDesc(noWindow));

    SwapChainDesc noExtent = desc;
    noExtent.surface.width = 0;
    CG_EXPECT_FALSE(ValidateSwapChainDesc(noExtent));

    SwapChainDesc oneImage = desc;
    oneImage.imageCount = 1;
    CG_EXPECT_FALSE(ValidateSwapChainDesc(oneImage));

    SwapChainDesc tooMany = desc;
    tooMany.imageCount = kMaxSwapChainImages + 1;
    CG_EXPECT_FALSE(ValidateSwapChainDesc(tooMany));

    SwapChainDesc badMode = desc;
    badMode.presentMode = PresentMode::Count;
    CG_EXPECT_FALSE(ValidateSwapChainDesc(badMode));

    // A depth format cannot be presented.
    SwapChainDesc depthFormat = desc;
    depthFormat.format = Format::D32Float;
    CG_EXPECT_FALSE(ValidateSwapChainDesc(depthFormat));

    // A mailbox description is structurally valid; the image-count rule is the
    // backend's, and `rhi_swapchain_present_modes` covers it.
    SwapChainDesc mailbox = desc;
    mailbox.presentMode = PresentMode::Mailbox;
    CG_EXPECT_TRUE(ValidateSwapChainDesc(mailbox).IsOk());
}

CG_TEST(rhi_swapchain_acquire_and_present)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    FakeSurface surface;
    auto created = fixture.device.CreateSwapChain(MakeDesc(surface));
    CG_REQUIRE(static_cast<bool>(created));
    SwapChain swapchain = std::move(created.Value());
    CG_EXPECT_TRUE(swapchain.IsValid());

    const SwapChainInfo info = swapchain.Info();
    CG_EXPECT_TRUE(info.valid);
    CG_EXPECT_EQ(info.width, u32{320});
    CG_EXPECT_EQ(info.height, u32{200});
    CG_EXPECT_EQ(info.imageCount, u32{2});
    CG_EXPECT_EQ(info.format, Format::BGRA8UnormSrgb);
    CG_EXPECT_EQ(info.presentMode, PresentMode::Fifo);
    CG_EXPECT_FALSE(info.acquired);

    // Presenting without an image is a caller bug.
    CG_EXPECT_FALSE(swapchain.Present());

    auto first = swapchain.AcquireNextImage();
    CG_REQUIRE(static_cast<bool>(first));
    CG_EXPECT_EQ(first.Value().imageIndex, u32{0});
    CG_EXPECT_TRUE(first.Value().texture.IsValid());
    CG_EXPECT_TRUE(first.Value().view.IsValid());
    // The image is a real texture the device can describe.
    CG_EXPECT_TRUE(fixture.device.TextureInfoOf(first.Value().texture).valid);
    CG_EXPECT_TRUE(swapchain.Info().acquired);

    // An image is acquired until it is presented.
    const auto second = swapchain.AcquireNextImage();
    CG_EXPECT_FALSE(second);
    if (!second)
    {
        CG_EXPECT_EQ(second.Error().code, ErrorCode::AlreadyExists);
    }

    CG_EXPECT_TRUE(swapchain.Present().IsOk());
    CG_EXPECT_FALSE(swapchain.Info().acquired);
    // Presenting hands the image to the display engine.
    CG_EXPECT_EQ(fixture.device.TextureStateOf(first.Value().texture, 0, 0),
                 ResourceState::Present);
    CG_EXPECT_FALSE(swapchain.Present());

    auto secondFrame = swapchain.AcquireNextImage();
    CG_REQUIRE(static_cast<bool>(secondFrame));
    CG_EXPECT_EQ(secondFrame.Value().imageIndex, u32{1});
    CG_REQUIRE(swapchain.Present().IsOk());

    // The ring wraps back to the first image.
    auto thirdFrame = swapchain.AcquireNextImage();
    CG_REQUIRE(static_cast<bool>(thirdFrame));
    CG_EXPECT_EQ(thirdFrame.Value().imageIndex, u32{0});
}

CG_TEST(rhi_swapchain_resize_replaces_images)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    FakeSurface surface;
    auto created = fixture.device.CreateSwapChain(MakeDesc(surface));
    CG_REQUIRE(static_cast<bool>(created));
    SwapChain swapchain = std::move(created.Value());

    auto frame = swapchain.AcquireNextImage();
    CG_REQUIRE(static_cast<bool>(frame));
    const TextureHandle oldImage = frame.Value().texture;
    const TextureViewHandle oldView = frame.Value().view;

    CG_EXPECT_TRUE(swapchain.Resize(640, 480).IsOk());
    const SwapChainInfo info = swapchain.Info();
    CG_EXPECT_EQ(info.width, u32{640});
    CG_EXPECT_EQ(info.height, u32{480});
    CG_EXPECT_FALSE(info.acquired);

    // Every handle over an old image is stale, so a caller that kept one cannot
    // draw into a destroyed image.
    CG_EXPECT_FALSE(fixture.device.TextureInfoOf(oldImage).valid);
    CG_EXPECT_FALSE(fixture.device.TextureViewInfoOf(oldView).valid);

    auto resized = swapchain.AcquireNextImage();
    CG_REQUIRE(static_cast<bool>(resized));
    CG_EXPECT_EQ(resized.Value().imageIndex, u32{0});
    CG_EXPECT_TRUE(fixture.device.TextureInfoOf(resized.Value().texture).valid);

    // A resize needs a real extent.
    CG_EXPECT_FALSE(swapchain.Resize(0, 480));
    CG_EXPECT_FALSE(swapchain.Resize(640, 0));
}

CG_TEST(rhi_swapchain_present_modes)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    FakeSurface surface;

    // Mailbox with two images would have to wait for the displayed image, so the
    // backend refuses the description instead of silently behaving like fifo.
    SwapChainDesc mailbox = MakeDesc(surface);
    mailbox.presentMode = PresentMode::Mailbox;
    const auto twoImages = fixture.device.CreateSwapChain(mailbox);
    CG_EXPECT_FALSE(twoImages);
    if (!twoImages)
    {
        CG_EXPECT_EQ(twoImages.Error().code, ErrorCode::InvalidArgument);
    }

    mailbox.imageCount = 3;
    auto triple = fixture.device.CreateSwapChain(mailbox);
    CG_REQUIRE(static_cast<bool>(triple));
    CG_EXPECT_EQ(triple.Value().Info().presentMode, PresentMode::Mailbox);
    CG_EXPECT_EQ(triple.Value().Info().imageCount, u32{3});

    // The null backend can tear, so immediate present is available.
    SwapChainDesc immediate = MakeDesc(surface);
    immediate.presentMode = PresentMode::Immediate;
    auto tearing = fixture.device.CreateSwapChain(immediate);
    CG_REQUIRE(static_cast<bool>(tearing));
    CG_EXPECT_EQ(tearing.Value().Info().presentMode, PresentMode::Immediate);

    CG_EXPECT_TRUE(PresentModeTears(PresentMode::Immediate));
    CG_EXPECT_FALSE(PresentModeTears(PresentMode::Fifo));
    CG_EXPECT_FALSE(PresentModeTears(PresentMode::Mailbox));
    CG_EXPECT_STREQ(PresentModeName(PresentMode::Fifo), "fifo");
    CG_EXPECT_STREQ(PresentModeName(PresentMode::Mailbox), "mailbox");
    CG_EXPECT_STREQ(PresentModeName(PresentMode::Immediate), "immediate");
    CG_EXPECT_STREQ(PresentModeName(PresentMode::Count), "unknown");
}

CG_TEST(rhi_swapchain_destroy_releases_images)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    FakeSurface surface;
    TextureHandle image;
    {
        auto created = fixture.device.CreateSwapChain(MakeDesc(surface));
        CG_REQUIRE(static_cast<bool>(created));
        SwapChain swapchain = std::move(created.Value());
        auto frame = swapchain.AcquireNextImage();
        CG_REQUIRE(static_cast<bool>(frame));
        image = frame.Value().texture;
        // The swapchain is destroyed at the end of this scope, which must release
        // the images it owns.
    }
    CG_EXPECT_FALSE(fixture.device.TextureInfoOf(image).valid);

    // An empty swapchain rejects every call.
    SwapChain empty;
    CG_EXPECT_FALSE(empty.IsValid());
    CG_EXPECT_FALSE(empty.Info().valid);
    CG_EXPECT_FALSE(empty.Present());
    CG_EXPECT_FALSE(empty.Resize(100, 100));
    const auto frame = empty.AcquireNextImage();
    CG_EXPECT_FALSE(frame);
}
