#pragma once

#include <cyber/core/types.h>

namespace cyber::core
{

// Stable codes for failures that are shared across modules. Modules may define
// their own enums when they need domain-specific detail.
enum class ErrorCode : u32
{
    Ok = 0,
    InvalidArgument,
    OutOfRange,
    NotFound,
    AlreadyExists,
    Unsupported,
    ParseError,
    Overflow,
    Internal,
};

constexpr const char* ErrorCodeName(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::Ok:
        return "ok";
    case ErrorCode::InvalidArgument:
        return "invalid_argument";
    case ErrorCode::OutOfRange:
        return "out_of_range";
    case ErrorCode::NotFound:
        return "not_found";
    case ErrorCode::AlreadyExists:
        return "already_exists";
    case ErrorCode::Unsupported:
        return "unsupported";
    case ErrorCode::ParseError:
        return "parse_error";
    case ErrorCode::Overflow:
        return "overflow";
    case ErrorCode::Internal:
        return "internal";
    }
    return "unknown";
}

constexpr bool IsOk(ErrorCode code) noexcept
{
    return code == ErrorCode::Ok;
}

// Small, trivially copyable error value. The message is a non-owning pointer to
// a string with static lifetime; callers must not pass a temporary buffer.
struct Error
{
    ErrorCode code = ErrorCode::Internal;
    const char* message = nullptr;
};

}  // namespace cyber::core
