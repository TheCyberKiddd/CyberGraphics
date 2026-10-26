#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/format.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/texture.h>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u8;

struct DeviceInterface;

// How a presented image reaches the screen, and therefore how much latency the
// swapchain adds.
enum class PresentMode : u8
{
    // Wait for the display's refresh before presenting. One frame of latency,
    // no tearing, and the only mode every API guarantees.
    Fifo = 0,
    // Present into a queue that replaces the pending image. No tearing, and a
    // newer frame discards an older unshown one. Needs at least three images,
    // because one is being displayed, one is pending, and one is being drawn.
    Mailbox,
    // Present immediately. Tearing is possible, latency is the lowest. Requires
    // `Feature::SwapchainTearing`, because a compositor that does not support it
    // will silently stretch one frame over several refreshes.
    Immediate,
    // Count of enumerators, for table tests. Not a mode.
    Count
};

// Number of real present modes, not counting `Count`.
inline constexpr u32 kPresentModeCount = static_cast<u32>(PresentMode::Count);

// Human-readable present mode, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* PresentModeName(PresentMode mode) noexcept
{
    switch (mode)
    {
    case PresentMode::Fifo:
        return "fifo";
    case PresentMode::Mailbox:
        return "mailbox";
    case PresentMode::Immediate:
        return "immediate";
    case PresentMode::Count:
        break;
    }
    return "unknown";
}

// Fewest images a swapchain may hold. Two is the minimum every API accepts and
// the minimum that lets the GPU draw into one image while another is displayed.
inline constexpr u32 kMinSwapChainImages = 2;

// Most images a swapchain may hold. More than three adds latency without adding
// throughput on any backend this project targets.
inline constexpr u32 kMaxSwapChainImages = 4;

// Description of a swapchain.
//
// The RHI never creates or destroys the surface: `cyber_platform` owns the
// window and the surface crosses the boundary as an opaque native handle that
// must outlive the swapchain.
struct SwapChainDesc
{
    // Surface to present into. A null native window or a zero extent is invalid.
    SurfaceDesc surface{};
    // Format of the images. Must be a color format that can be a render target.
    // `BGRA8UnormSrgb` is what both Windows and Metal prefer for a window.
    Format format = Format::BGRA8UnormSrgb;
    // Images in the ring, from `kMinSwapChainImages` to `kMaxSwapChainImages`.
    u32 imageCount = 2;
    // How presenting behaves.
    PresentMode presentMode = PresentMode::Fifo;
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

// One image handed out by `SwapChain::AcquireNextImage`.
//
// The handles are the texture itself and a color view over it, so a caller can
// put the image into a render pass with no extra object. They stay valid until
// the next `Resize`, which replaces every image.
struct SwapChainFrame
{
    // False when no image was acquired; every other field is zero.
    bool valid = false;
    // Index of the image in the ring, from 0 to `imageCount - 1`.
    u32 imageIndex = 0;
    // Texture holding the image.
    TextureHandle texture;
    // Color view over the whole image, ready to be a render pass attachment.
    TextureViewHandle view;
};

// What a swapchain reports about itself. `valid` is false for a stale handle, in
// which case every other field is zero.
struct SwapChainInfo
{
    bool valid = false;
    Format format = Format::BGRA8Unorm;
    u32 width = 0;
    u32 height = 0;
    u32 imageCount = 0;
    // Index of the image currently acquired, or 0 when none is.
    u32 currentImage = 0;
    PresentMode presentMode = PresentMode::Fifo;
    // True while an image is acquired and not yet presented.
    bool acquired = false;
};

// Per-swapchain function table. Every entry is required: a null entry is a
// backend bug that swapchain creation rejects.
struct SwapChainInterface
{
    // Acquires the next image and writes its handles to `out`, which must be
    // non-null. Returns `ErrorCode::AlreadyExists` when an image is already
    // acquired, and `ErrorCode::NotFound` when the surface is not available
    // (minimised, for example); the caller retries on the next frame.
    Status<Error> (*acquireNextImage)(void* internal, SwapChainFrame* out) noexcept;
    // Presents the acquired image. Returns `ErrorCode::InvalidArgument` when no
    // image is acquired.
    Status<Error> (*present)(void* internal) noexcept;
    // Recreates every image at a new extent. Every handle a caller kept is
    // invalidated. A zero extent is `ErrorCode::OutOfRange`.
    Status<Error> (*resize)(void* internal, u32 width, u32 height) noexcept;
    // Reports size, format, present mode, and acquire state.
    SwapChainInfo (*info)(const void* internal) noexcept;
};

// What `createSwapChain` hands back. Both members are required.
struct SwapChainCreation
{
    // Per-swapchain function table, with static lifetime.
    const SwapChainInterface* interface = nullptr;
    // Backend-private swapchain state, owned by the backend.
    void* internal = nullptr;
};

// A ring of presentable images.
//
// The swapchain is move-only and owns its backend state: destroying it releases
// the images. It must not outlive its device, and its surface must outlive it.
class SwapChain
{
public:
    SwapChain() noexcept = default;
    ~SwapChain();

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    SwapChain(SwapChain&& other) noexcept;
    SwapChain& operator=(SwapChain&& other) noexcept;

    // True when this swapchain owns backend state.
    [[nodiscard]] bool IsValid() const noexcept;

    // Generation-counted identity, for logging and for a device-side look-up.
    [[nodiscard]] SwapChainHandle Handle() const noexcept;

    // Size, format, present mode, and acquire state.
    [[nodiscard]] SwapChainInfo Info() const noexcept;

    // Acquires the next image. Returns `ErrorCode::AlreadyExists` when an image
    // is still acquired, because presenting is what releases one, and
    // `ErrorCode::NotFound` when the surface cannot give an image this frame.
    [[nodiscard]] Result<SwapChainFrame, Error> AcquireNextImage() noexcept;

    // Presents the acquired image and releases it. Returns
    // `ErrorCode::InvalidArgument` when nothing is acquired.
    [[nodiscard]] Status<Error> Present() noexcept;

    // Recreates every image at a new extent in pixels. The extent must be
    // non-zero and within the device's limits; every handle over the old images
    // becomes stale.
    [[nodiscard]] Status<Error> Resize(u32 width, u32 height) noexcept;

private:
    friend class Device;

    void Release() noexcept;
    void MoveFrom(SwapChain& other) noexcept;

    const DeviceInterface* m_device = nullptr;
    void* m_deviceInternal = nullptr;
    const SwapChainInterface* m_interface = nullptr;
    void* m_internal = nullptr;
    SwapChainHandle m_handle;
};

// Checks the parts of a swapchain description the RHI can check without the
// backend: a surface with a native window and a non-zero extent, an image count
// inside the allowed range, a real present mode, and a format that can be a
// render target.
//
// The checks that need device limits and features — extent against
// `Limits::maxTexture2D`, `Immediate` against `Feature::SwapchainTearing` — are
// the backend's, because only it knows what it can present.
[[nodiscard]] Status<Error> ValidateSwapChainDesc(const SwapChainDesc& desc) noexcept;

// True when `mode` may present without waiting for the display. These are the
// modes that need `Feature::SwapchainTearing` or a queue that can replace a
// pending image.
[[nodiscard]] constexpr bool PresentModeTears(PresentMode mode) noexcept
{
    return mode == PresentMode::Immediate;
}

}  // namespace cyber::rhi
