#include <cyber/platform/time.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cyber::platform
{

namespace
{

constexpr int kMaxSleepAttempts = 100;
constexpr f64 kMaxSleepSliceSeconds = 3600.0;

}  // namespace

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

void SleepUntilSeconds(f64 deadlineSeconds) noexcept
{
    for (int attempt = 0; attempt < kMaxSleepAttempts; ++attempt)
    {
        const f64 remaining = deadlineSeconds - Clock::NowSeconds();
        if (!(remaining > 0.0))
        {
            return;
        }

        HANDLE timer = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (timer == nullptr)
        {
            f64 milliseconds = remaining * 1000.0;
            if (!(milliseconds >= 1.0))
            {
                milliseconds = 1.0;
            }
            if (milliseconds > kMaxSleepSliceSeconds * 1000.0)
            {
                milliseconds = kMaxSleepSliceSeconds * 1000.0;
            }
            Sleep(static_cast<DWORD>(milliseconds));
            continue;
        }

        f64 slice = remaining;
        if (slice > kMaxSleepSliceSeconds)
        {
            slice = kMaxSleepSliceSeconds;
        }
        // A negative due time is relative, in 100-nanosecond units.
        f64 hundredNanoseconds = slice * 10000000.0;
        if (!(hundredNanoseconds >= 1.0))
        {
            hundredNanoseconds = 1.0;
        }
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(hundredNanoseconds);
        if (due.QuadPart >= 0)
        {
            due.QuadPart = -1;
        }

        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != 0)
        {
            WaitForSingleObject(timer, INFINITE);
        }
        else
        {
            Sleep(1);
        }
        CloseHandle(timer);
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
