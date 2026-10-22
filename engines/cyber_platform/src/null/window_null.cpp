#include "../window_impl.h"

namespace cyber::platform
{

core::Result<WindowImpl*, core::Error> CreateWindowImpl(const WindowDesc& desc,
                                                        memory::Allocator& allocator) noexcept
{
    (void)desc;
    (void)allocator;
    return core::Result<WindowImpl*, core::Error>::Err(core::Error{
        core::ErrorCode::Unsupported, "no window backend is available on this platform"});
}

bool IsWindowSystemSupported() noexcept
{
    return false;
}

}  // namespace cyber::platform
