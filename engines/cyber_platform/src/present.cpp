#include "present_impl.h"

#include <cyber/platform/present.h>

#include <utility>

namespace cyber::platform
{

Presenter::Presenter(PresenterImpl* impl) noexcept : m_impl(impl) {}

Presenter::~Presenter()
{
    if (m_impl != nullptr)
    {
        memory::Allocator* allocator = m_impl->allocator;
        m_impl->Destroy();
        m_impl->~PresenterImpl();
        if (allocator != nullptr)
        {
            allocator->Deallocate(m_impl);
        }
        m_impl = nullptr;
    }
}

Presenter::Presenter(Presenter&& other) noexcept : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

Presenter& Presenter::operator=(Presenter&& other) noexcept
{
    if (this != &other)
    {
        if (m_impl != nullptr)
        {
            memory::Allocator* allocator = m_impl->allocator;
            m_impl->Destroy();
            m_impl->~PresenterImpl();
            if (allocator != nullptr)
            {
                allocator->Deallocate(m_impl);
            }
        }
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

core::Result<Presenter, core::Error> Presenter::Create(Window& window,
                                                       memory::Allocator& allocator) noexcept
{
    core::Result<PresenterImpl*, core::Error> result = CreatePresenterImpl(window, allocator);
    if (!result)
    {
        return core::Result<Presenter, core::Error>::Err(result.Error());
    }
    return core::Result<Presenter, core::Error>::Ok(Presenter(result.Value()));
}

core::Status<core::Error> Presenter::Present(const PresentSource& source) noexcept
{
    if (m_impl == nullptr)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Unsupported, "presenter is not created"});
    }
    return m_impl->Present(source);
}

bool Presenter::IsValid() const noexcept
{
    return m_impl != nullptr;
}

}  // namespace cyber::platform
