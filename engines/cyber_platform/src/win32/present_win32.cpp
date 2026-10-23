#include "../present_impl.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cyber/platform/present.h>
#include <cyber/platform/window.h>

#include <new>
#include <windows.h>

namespace cyber::platform
{

namespace
{

constexpr usize kScratchAlignment = 16;

core::Status<core::Error> InvalidSource(const char* message) noexcept
{
    return core::Status<core::Error>::Err(core::Error{core::ErrorCode::InvalidArgument, message});
}

bool ValidateSource(const PresentSource& source, usize& stride) noexcept
{
    if (source.width <= 0 || source.height <= 0)
    {
        return false;
    }
    if (source.format != PresentFormat::RGBA8Unorm)
    {
        return false;
    }

    const usize rowBytes = static_cast<usize>(source.width) * 4;
    stride = source.strideBytes == 0 ? rowBytes : source.strideBytes;
    if (stride < rowBytes)
    {
        return false;
    }
    if (source.pixels.Data() == nullptr)
    {
        return false;
    }

    const usize rows = static_cast<usize>(source.height) - 1;
    if (rows > 0 && stride > (kInvalidIndex - rowBytes) / rows)
    {
        return false;
    }
    const usize required = rows * stride + rowBytes;
    return source.pixels.Size() >= required;
}

class PresenterImplWin32 final : public PresenterImpl
{
public:
    explicit PresenterImplWin32(memory::Allocator& allocator) noexcept : PresenterImpl(allocator) {}

    ~PresenterImplWin32() override
    {
        Destroy();
    }

    core::Status<core::Error> Initialize(Window& window) noexcept
    {
        if (!window.IsOpen() || window.NativeHandle() == nullptr)
        {
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Unsupported, "window is not open"});
        }
        m_hwnd = static_cast<HWND>(window.NativeHandle());
        return core::Status<core::Error>::Ok();
    }

    core::Status<core::Error> Present(const PresentSource& source) noexcept override
    {
        usize stride = 0;
        if (!ValidateSource(source, stride))
        {
            return InvalidSource("invalid present source");
        }
        if (m_hwnd == nullptr || IsWindow(m_hwnd) == 0)
        {
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Unsupported, "window is not open"});
        }

        RECT client{};
        if (GetClientRect(m_hwnd, &client) == 0)
        {
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Internal, "GetClientRect failed"});
        }
        const i32 clientWidth = client.right - client.left;
        const i32 clientHeight = client.bottom - client.top;
        if (clientWidth <= 0 || clientHeight <= 0)
        {
            return core::Status<core::Error>::Ok();
        }

        const usize clientPixels =
            static_cast<usize>(clientWidth) * static_cast<usize>(clientHeight);
        if (clientPixels > kInvalidIndex / 4)
        {
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Overflow, "client area is too large"});
        }
        const usize scratchBytes = clientPixels * 4;
        if (scratchBytes > m_scratchBytes)
        {
            if (m_scratch != nullptr)
            {
                allocator->Deallocate(m_scratch);
                m_scratch = nullptr;
                m_scratchBytes = 0;
            }
            m_scratch = static_cast<u8*>(allocator->Allocate(scratchBytes, kScratchAlignment));
            if (m_scratch == nullptr)
            {
                return core::Status<core::Error>::Err(core::Error{
                    core::ErrorCode::Internal, "failed to allocate the present scratch buffer"});
            }
            m_scratchBytes = scratchBytes;
        }

        const u8* sourcePixels = reinterpret_cast<const u8*>(source.pixels.Data());
        // Fixed-point column stepping avoids a 64-bit division per pixel. When
        // the client and source widths match, the step is exactly one pixel.
        const u64 columnStep =
            clientWidth == source.width
                ? (1ull << 32)
                : (static_cast<u64>(source.width) << 32) / static_cast<u64>(clientWidth);
        for (i32 y = 0; y < clientHeight; ++y)
        {
            const u64 sourceY = static_cast<u64>(y) * static_cast<u64>(source.height) /
                                static_cast<u64>(clientHeight);
            const u8* sourceRow = sourcePixels + static_cast<usize>(sourceY) * stride;
            u8* destination =
                m_scratch + static_cast<usize>(y) * static_cast<usize>(clientWidth) * 4;
            u64 column = 0;
            for (i32 x = 0; x < clientWidth; ++x)
            {
                const u8* pixel = sourceRow + static_cast<usize>(column >> 32) * 4;
                // DIB 32-bit pixels are BGRA; alpha is unused by BI_RGB.
                destination[0] = pixel[2];
                destination[1] = pixel[1];
                destination[2] = pixel[0];
                destination[3] = 255;
                destination += 4;
                column += columnStep;
            }
        }

        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = clientWidth;
        bitmap.bmiHeader.biHeight = -clientHeight;  // Top-down rows.
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;

        HDC deviceContext = GetDC(m_hwnd);
        if (deviceContext == nullptr)
        {
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Internal, "GetDC failed"});
        }
        const int copied =
            StretchDIBits(deviceContext, 0, 0, clientWidth, clientHeight, 0, 0, clientWidth,
                          clientHeight, m_scratch, &bitmap, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(m_hwnd, deviceContext);

        if (copied <= 0)
        {
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Internal, "StretchDIBits failed"});
        }
        return core::Status<core::Error>::Ok();
    }

    void Destroy() noexcept override
    {
        if (m_scratch != nullptr && allocator != nullptr)
        {
            allocator->Deallocate(m_scratch);
        }
        m_scratch = nullptr;
        m_scratchBytes = 0;
        m_hwnd = nullptr;
    }

private:
    HWND m_hwnd = nullptr;
    u8* m_scratch = nullptr;
    usize m_scratchBytes = 0;
};

}  // namespace

core::Result<PresenterImpl*, core::Error> CreatePresenterImpl(Window& window,
                                                              memory::Allocator& allocator) noexcept
{
    void* memory = allocator.Allocate(sizeof(PresenterImplWin32), alignof(PresenterImplWin32));
    if (memory == nullptr)
    {
        return core::Result<PresenterImpl*, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate the presenter"});
    }

    auto* impl = static_cast<PresenterImplWin32*>(memory);
    std::construct_at(impl, allocator);
    core::Status<core::Error> status = impl->Initialize(window);
    if (!status)
    {
        impl->Destroy();
        impl->~PresenterImplWin32();
        allocator.Deallocate(memory);
        return core::Result<PresenterImpl*, core::Error>::Err(status.Error());
    }
    return core::Result<PresenterImpl*, core::Error>::Ok(static_cast<PresenterImpl*>(impl));
}

}  // namespace cyber::platform
