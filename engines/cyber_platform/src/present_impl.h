#pragma once

#include <cyber/platform/present.h>

namespace cyber::platform
{

// Backend state for Presenter. The public wrapper owns one implementation
// allocated from the caller's allocator.
class PresenterImpl
{
public:
    explicit PresenterImpl(memory::Allocator& allocator) noexcept : allocator(&allocator) {}
    virtual ~PresenterImpl() = default;

    PresenterImpl(const PresenterImpl&) = delete;
    PresenterImpl& operator=(const PresenterImpl&) = delete;

    virtual core::Status<core::Error> Present(const PresentSource& source) noexcept = 0;
    virtual void Destroy() noexcept = 0;

    memory::Allocator* allocator = nullptr;
};

[[nodiscard]] core::Result<PresenterImpl*, core::Error>
CreatePresenterImpl(Window& window, memory::Allocator& allocator) noexcept;

}  // namespace cyber::platform
