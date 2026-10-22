#include <cyber/platform/time.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cyber::platform
{

u64 Clock::NowNanoseconds() noexcept
{
    LARGE_INTEGER frequency{};
    LARGE_INTEGER counter{};
    if (QueryPerformanceFrequency(&frequency) == 0 || frequency.QuadPart <= 0 ||
        QueryPerformanceCounter(&counter) == 0)
    {
        return 0;
    }

    const long double seconds =
        static_cast<long double>(counter.QuadPart) / static_cast<long double>(frequency.QuadPart);
    if (seconds <= 0.0L)
    {
        return 0;
    }
    return static_cast<u64>(seconds * 1.0e9L);
}

}  // namespace cyber::platform
