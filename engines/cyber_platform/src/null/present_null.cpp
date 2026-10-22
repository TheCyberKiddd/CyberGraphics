#include "../present_impl.h"

namespace cyber::platform
{

core::Result<PresenterImpl*, core::Error> CreatePresenterImpl(Window& window,
                                                              memory::Allocator& allocator) noexcept
{
    (void)window;
    (void)allocator;
    return core::Result<PresenterImpl*, core::Error>::Err(core::Error{
        core::ErrorCode::Unsupported, "no present backend is available on this platform"});
}

}  // namespace cyber::platform
