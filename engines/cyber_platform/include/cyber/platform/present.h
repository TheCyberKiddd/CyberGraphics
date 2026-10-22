#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/types.h>
#include <cyber/platform/types.h>

namespace cyber::platform
{

class Window;
class PresenterImpl;

// Memory layout of a present source. RGBA8Unorm means four bytes per pixel in
// memory order red, green, blue, alpha. The platform performs no color-space
// conversion; producers encode sRGB before presenting.
enum class PresentFormat : u8
{
    RGBA8Unorm = 0,
};

// A caller-owned image ready to be shown. pixels must contain at least
// strideBytes * (height - 1) + width * 4 bytes. strideBytes 0 means tightly
// packed (width * 4). The view is non-owning and is read only during Present.
struct PresentSource
{
    memory::Span<const memory::Byte> pixels{};
    i32 width = 0;
    i32 height = 0;
    usize strideBytes = 0;
    PresentFormat format = PresentFormat::RGBA8Unorm;
};

// Blits caller-owned pixels into a window client area through the OS.
//
// The presenter does not own pixels, never includes renderer headers, and does
// no color conversion. It stretches the source to the current client size with
// nearest-neighbour sampling, reuses one allocator-backed scratch buffer, and
// skips a minimized window (zero client area) by returning Ok.
//
// The presenter stores an opaque native window handle. It must not outlive the
// window it was created from. Presenter is move-only.
class Presenter
{
public:
    Presenter() noexcept = default;
    ~Presenter();

    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    Presenter(Presenter&& other) noexcept;
    Presenter& operator=(Presenter&& other) noexcept;

    // Requires an open window. Unsupported platforms return
    // ErrorCode::Unsupported; a closed or default-constructed window returns
    // ErrorCode::Unsupported as well.
    [[nodiscard]] static core::Result<Presenter, core::Error>
    Create(Window& window, memory::Allocator& allocator) noexcept;

    // Copies and shows the image. Fails with InvalidArgument when the source is
    // empty, has non-positive dimensions, an unknown layout, or an inconsistent
    // stride. Fails with Unsupported when the window has been closed since
    // creation. Allocation failure while growing the scratch buffer returns
    // Internal.
    [[nodiscard]] core::Status<core::Error> Present(const PresentSource& source) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;

private:
    explicit Presenter(PresenterImpl* impl) noexcept;

    PresenterImpl* m_impl = nullptr;
};

}  // namespace cyber::platform
