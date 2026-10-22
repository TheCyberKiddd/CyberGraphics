#include <cyber/platform/thread.h>

#include <cstring>
#include <functional>
#include <pthread.h>
#include <thread>

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

void* ThreadTrampoline(void* parameter)
{
    auto* context = static_cast<StartContext*>(parameter);
    Thread::SetCurrentThreadName(context->name);
    context->function(context->userData);
    return nullptr;
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
    static_assert(sizeof(pthread_t) <= sizeof(u64), "pthread_t does not fit in the thread handle");

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

    pthread_t thread{};
    const int result = pthread_create(&thread, nullptr, &ThreadTrampoline, context);
    if (result != 0)
    {
        allocator.Deallocate(context);
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "pthread_create failed"});
    }

    std::memcpy(&m_handle, &thread, sizeof(thread));
    m_id = m_handle;
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
    pthread_t thread{};
    std::memcpy(&thread, &m_handle, sizeof(thread));
    pthread_join(thread, nullptr);
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
    char truncated[16] = {};
    usize index = 0;
    while (name[index] != '\0' && index + 1 < sizeof(truncated))
    {
        truncated[index] = name[index];
        ++index;
    }
    truncated[index] = '\0';
    pthread_setname_np(pthread_self(), truncated);
}

u64 Thread::CurrentThreadId() noexcept
{
    return static_cast<u64>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

}  // namespace cyber::platform
