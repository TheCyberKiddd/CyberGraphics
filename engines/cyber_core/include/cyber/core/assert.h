#pragma once

#include <cyber/core/types.h>

namespace cyber::core
{

// Formats one assertion line into a caller-provided buffer without allocating.
// Returns the number of characters the full message would need, excluding the
// null terminator, following std::snprintf semantics. A null buffer or a zero
// capacity performs no write.
usize FormatAssertionMessage(char* buffer, usize capacity, const char* expression, const char* file,
                             int line, const char* message) noexcept;

// Writes a formatted failure to stderr, breaks into the debugger when one is
// available, and terminates the process. No heap allocation is performed.
[[noreturn]] void AssertFailed(const char* expression, const char* file, int line,
                               const char* message) noexcept;

// Requests a debugger break. Returns on platforms without an intrinsic; callers
// that need termination should call std::abort afterwards.
void BreakIntoDebugger() noexcept;

}  // namespace cyber::core

#if defined(CG_SHIPPING)

#define CG_ASSERT(expression) ((void)0)
#define CG_ASSERT_MSG(expression, message) ((void)0)
#define CG_VERIFY(expression) ((void)(expression))

#else

#define CG_ASSERT(expression)                                                                      \
    ((expression) ? (void)0 : ::cyber::core::AssertFailed(#expression, __FILE__, __LINE__, nullptr))

#define CG_ASSERT_MSG(expression, message)                                                         \
    ((expression) ? (void)0                                                                        \
                  : ::cyber::core::AssertFailed(#expression, __FILE__, __LINE__, (message)))

#define CG_VERIFY(expression) CG_ASSERT(expression)

#endif
