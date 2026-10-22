#include <cyber/platform/time.h>

#include <cerrno>
#include <ctime>

namespace cyber::platform
{

u64 Clock::NowNanoseconds() noexcept
{
    struct timespec time{};
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    {
        return 0;
    }
    if (time.tv_sec < 0 || time.tv_nsec < 0)
    {
        return 0;
    }
    return static_cast<u64>(time.tv_sec) * 1000000000ULL + static_cast<u64>(time.tv_nsec);
}

}  // namespace cyber::platform
