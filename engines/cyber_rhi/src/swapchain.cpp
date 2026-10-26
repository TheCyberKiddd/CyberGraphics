// The swapchain value class and the description rules every backend shares.
//
// What lives here is what a description can tell on its own: a surface exists, an
// extent is non-zero, the image count and present mode are real, and the format
// can be a render target. Whether the device can actually present at that extent
// or in that mode is the backend's answer, and it is the one that reports
// `Unsupported`.

#include <cyber/rhi/backend.h>
#include <cyber/rhi/swapchain.h>

#include <utility>

namespace cyber::rhi
{

namespace
{

using cyber::core::ErrorCode;

[[nodiscard]] Status<Error> Fail(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

}  // namespace

Status<Error> ValidateSwapChainDesc(const SwapChainDesc& desc) noexcept
{
    if (desc.surface.nativeWindow == nullptr)
    {
        return Fail(ErrorCode::InvalidArgument, "a swapchain needs a native window");
    }
    if (desc.surface.width == 0 || desc.surface.height == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "a swapchain surface has a zero extent");
    }
    if (desc.imageCount < kMinSwapChainImages)
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a swapchain needs at least kMinSwapChainImages images");
    }
    if (desc.imageCount > kMaxSwapChainImages)
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a swapchain has more images than kMaxSwapChainImages");
    }
    if (static_cast<u32>(desc.presentMode) >= kPresentModeCount)
    {
        return Fail(ErrorCode::InvalidArgument, "a swapchain present mode is not a mode");
    }
    if (!FormatSupportsRenderTarget(desc.format))
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a swapchain format cannot be a color render target");
    }
    return Status<Error>::Ok();
}

SwapChain::~SwapChain()
{
    Release();
}

SwapChain::SwapChain(SwapChain&& other) noexcept
{
    MoveFrom(other);
}

SwapChain& SwapChain::operator=(SwapChain&& other) noexcept
{
    if (this != &other)
    {
        Release();
        MoveFrom(other);
    }
    return *this;
}

bool SwapChain::IsValid() const noexcept
{
    return m_interface != nullptr && m_internal != nullptr && m_handle.IsValid();
}

SwapChainHandle SwapChain::Handle() const noexcept
{
    return m_handle;
}

SwapChainInfo SwapChain::Info() const noexcept
{
    if (!IsValid() || m_interface->info == nullptr)
    {
        return SwapChainInfo{};
    }
    return m_interface->info(m_internal);
}

Result<SwapChainFrame, Error> SwapChain::AcquireNextImage() noexcept
{
    if (!IsValid())
    {
        return Result<SwapChainFrame, Error>::Err(
            Error{ErrorCode::InvalidArgument, "swapchain is empty"});
    }

    SwapChainFrame frame;
    const Status<Error> acquired = m_interface->acquireNextImage(m_internal, &frame);
    if (!acquired)
    {
        return Result<SwapChainFrame, Error>::Err(acquired.Error());
    }
    if (!frame.valid || !frame.texture.IsValid() || !frame.view.IsValid())
    {
        return Result<SwapChainFrame, Error>::Err(
            Error{ErrorCode::Internal, "backend returned an incomplete swapchain frame"});
    }
    return Result<SwapChainFrame, Error>::Ok(frame);
}

Status<Error> SwapChain::Present() noexcept
{
    if (!IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "swapchain is empty");
    }
    return m_interface->present(m_internal);
}

Status<Error> SwapChain::Resize(u32 width, u32 height) noexcept
{
    if (!IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "swapchain is empty");
    }
    return m_interface->resize(m_internal, width, height);
}

void SwapChain::Release() noexcept
{
    if (m_device != nullptr && m_deviceInternal != nullptr && m_handle.IsValid() &&
        m_device->destroySwapChain != nullptr)
    {
        (void)m_device->destroySwapChain(m_deviceInternal, m_handle);
    }
    m_device = nullptr;
    m_deviceInternal = nullptr;
    m_interface = nullptr;
    m_internal = nullptr;
    m_handle.Reset();
}

void SwapChain::MoveFrom(SwapChain& other) noexcept
{
    m_device = other.m_device;
    m_deviceInternal = other.m_deviceInternal;
    m_interface = other.m_interface;
    m_internal = other.m_internal;
    m_handle = other.m_handle;

    other.m_device = nullptr;
    other.m_deviceInternal = nullptr;
    other.m_interface = nullptr;
    other.m_internal = nullptr;
    other.m_handle.Reset();
}

}  // namespace cyber::rhi
