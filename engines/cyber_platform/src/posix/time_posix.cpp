#include <cyber/platform/time.h>

#include <cerrno>
#include <ctime>

namespace cyber::platform
{

namespace
{

constexpr int kMaxSleepAttempts = 100;
constexpr f64 kMaxSleepSliceSeconds = 3600.0;

}  // namespace

u64 Clock::NowNanoseconds() noexcept
{
    struct timespec time = {};
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

void SleepUntilSeconds(f64 deadlineSeconds) noexcept
{
    for (int attempt = 0; attempt < kMaxSleepAttempts; ++attempt)
    {
        f64 remaining = deadlineSeconds - Clock::NowSeconds();
        if (!(remaining > 0.0))
        {
            return;
        }
        if (remaining > kMaxSleepSliceSeconds)
        {
            remaining = kMaxSleepSliceSeconds;
        }

        struct timespec request = {};
        request.tv_sec = static_cast<time_t>(remaining);
        const f64 fraction = remaining - static_cast<f64>(request.tv_sec);
        request.tv_nsec = static_cast<long>(fraction * 1.0e9);
        if (request.tv_nsec < 0)
        {
            request.tv_nsec = 0;
        }
        if (request.tv_nsec > 999999999)
        {
            request.tv_nsec = 999999999;
        }

        const int result = nanosleep(&request, nullptr);
        if (result != 0 && errno != EINTR)
        {
            return;
        }
    }
}

void SleepSeconds(f64 seconds) noexcept
{
    if (!(seconds > 0.0))
    {
        return;
    }
    SleepUntilSeconds(Clock::NowSeconds() + seconds);
}

}  // namespace cyber::platform
