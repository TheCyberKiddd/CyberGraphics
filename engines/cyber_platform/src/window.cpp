#include "window_impl.h"

#include <cyber/platform/window.h>

#include <utility>

namespace cyber::platform
{

namespace
{

const InputState& EmptyInput() noexcept
{
    static const InputState empty{};
    return empty;
}

}  // namespace

Window::Window(WindowImpl* impl) noexcept : m_impl(impl) {}

Window::~Window()
{
    if (m_impl != nullptr)
    {
        memory::Allocator* allocator = m_impl->allocator;
        m_impl->Destroy();
        m_impl->~WindowImpl();
        if (allocator != nullptr)
        {
            allocator->Deallocate(m_impl);
        }
        m_impl = nullptr;
    }
}

Window::Window(Window&& other) noexcept : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

Window& Window::operator=(Window&& other) noexcept
{
    if (this != &other)
    {
        if (m_impl != nullptr)
        {
            memory::Allocator* allocator = m_impl->allocator;
            m_impl->Destroy();
            m_impl->~WindowImpl();
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

core::Result<Window, core::Error> Window::Create(const WindowDesc& desc,
                                                 memory::Allocator& allocator) noexcept
{
    core::Result<WindowImpl*, core::Error> result = CreateWindowImpl(desc, allocator);
    if (!result)
    {
        return core::Result<Window, core::Error>::Err(result.Error());
    }
    return core::Result<Window, core::Error>::Ok(Window(result.Value()));
}

void Window::PollEvents() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->PollEvents();
    }
}

bool Window::PollEvent(WindowEvent& event) noexcept
{
    return m_impl != nullptr && m_impl->PollEvent(event);
}

void Window::WaitEvents() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->WaitEvents();
    }
}

void Window::BeginFrame() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->BeginFrame();
    }
}

bool Window::IsOpen() const noexcept
{
    return m_impl != nullptr && m_impl->IsOpen();
}

void Window::RequestClose() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->RequestClose();
    }
}

void Window::Resize(i32 width, i32 height) noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->Resize(width, height);
    }
}

core::Status<core::Error> Window::SetFullscreen(bool fullscreen) noexcept
{
    if (m_impl == nullptr)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Unsupported, "window is not open"});
    }
    return m_impl->SetFullscreen(fullscreen);
}

bool Window::IsFullscreen() const noexcept
{
    return m_impl != nullptr && m_impl->IsFullscreen();
}

void Window::SetTitle(const char* title) noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->SetTitle(title);
    }
}

i32 Window::Width() const noexcept
{
    return m_impl != nullptr ? m_impl->Width() : 0;
}

i32 Window::Height() const noexcept
{
    return m_impl != nullptr ? m_impl->Height() : 0;
}

f32 Window::DpiScale() const noexcept
{
    return m_impl != nullptr ? m_impl->DpiScale() : 1.0f;
}

const InputState& Window::Input() const noexcept
{
    if (m_impl == nullptr)
    {
        return EmptyInput();
    }
    return m_impl->Input();
}

void* Window::NativeHandle() const noexcept
{
    return m_impl != nullptr ? m_impl->NativeHandle() : nullptr;
}

}  // namespace cyber::platform
