#include <cyber/platform/thread.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cyber::platform
{

namespace
{

struct StartContext
{
    memory::Allocator* allocator = nullptr;
    ThreadFunction function = nullptr;
    void* userData = nullptr;
    char name[64] = {};
};

bool Utf8ToWide(const char* text, wchar_t* out, usize capacity) noexcept
{
    if (text == nullptr || out == nullptr || capacity == 0)
    {
        return false;
    }
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out,
                               static_cast<int>(capacity)) > 0;
}

DWORD WINAPI ThreadTrampoline(LPVOID parameter)
{
    auto* context = static_cast<StartContext*>(parameter);
    Thread::SetCurrentThreadName(context->name);
    context->function(context->userData);
    return 0;
}

}  // namespace

core::Status<core::Error> Thread::Start(memory::Allocator& allocator, const char* name,
                                        ThreadFunction function, void* userData) noexcept
{
    if (function == nullptr)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "thread function is null"});
    }
    if (m_joinable)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::AlreadyExists, "thread is already running"});
    }

    void* memory = allocator.Allocate(sizeof(StartContext), alignof(StartContext));
    if (memory == nullptr)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate thread context"});
    }

    auto* context = static_cast<StartContext*>(memory);
    context->allocator = &allocator;
    context->function = function;
    context->userData = userData;
    if (name != nullptr)
    {
        usize index = 0;
        while (name[index] != '\0' && index + 1 < sizeof(context->name))
        {
            context->name[index] = name[index];
            ++index;
        }
        context->name[index] = '\0';
    }

    DWORD threadId = 0;
    HANDLE handle = CreateThread(nullptr, 0, &ThreadTrampoline, context, 0, &threadId);
    if (handle == nullptr)
    {
        allocator.Deallocate(context);
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "CreateThread failed"});
    }

    m_handle = reinterpret_cast<u64>(handle);
    m_id = static_cast<u64>(threadId);
    m_joinable = true;
    m_context = context;
    m_allocator = &allocator;
    return core::Status<core::Error>::Ok();
}

void Thread::Join() noexcept
{
    if (!m_joinable)
    {
        return;
    }
    WaitForSingleObject(reinterpret_cast<HANDLE>(m_handle), INFINITE);
    CloseHandle(reinterpret_cast<HANDLE>(m_handle));
    m_handle = 0;
    m_id = 0;
    m_joinable = false;
    if (m_context != nullptr && m_allocator != nullptr)
    {
        m_allocator->Deallocate(m_context);
    }
    m_context = nullptr;
    m_allocator = nullptr;
}

void Thread::SetCurrentThreadName(const char* name) noexcept
{
    if (name == nullptr)
    {
        return;
    }
    wchar_t wideName[64] = {};
    if (Utf8ToWide(name, wideName, 64))
    {
        SetThreadDescription(GetCurrentThread(), wideName);
    }
}

u64 Thread::CurrentThreadId() noexcept
{
    return static_cast<u64>(GetCurrentThreadId());
}

}  // namespace cyber::platform
