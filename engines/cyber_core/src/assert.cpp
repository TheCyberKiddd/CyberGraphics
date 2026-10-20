#include <cyber/core/assert.h>

#include <cstdio>
#include <cstdlib>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace cyber::core
{

usize FormatAssertionMessage(char* buffer, usize capacity, const char* expression, const char* file,
                             int line, const char* message) noexcept
{
    if (buffer == nullptr || capacity == 0)
    {
        return 0;
    }

    const char* safeExpression = expression != nullptr ? expression : "<unknown>";
    const char* safeFile = file != nullptr ? file : "<unknown>";

    if (message != nullptr)
    {
        const int written =
            std::snprintf(buffer, capacity, "Assertion failed: %s\n  at %s:%d\n  message: %s",
                          safeExpression, safeFile, line, message);
        return written > 0 ? static_cast<usize>(written) : 0;
    }

    const int written = std::snprintf(buffer, capacity, "Assertion failed: %s\n  at %s:%d",
                                      safeExpression, safeFile, line);
    return written > 0 ? static_cast<usize>(written) : 0;
}

void BreakIntoDebugger() noexcept
{
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#endif
}

[[noreturn]] void AssertFailed(const char* expression, const char* file, int line,
                               const char* message) noexcept
{
    char buffer[1024];
    FormatAssertionMessage(buffer, sizeof(buffer), expression, file, line, message);
    std::fputs(buffer, stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    BreakIntoDebugger();
    std::abort();
}

}  // namespace cyber::core
