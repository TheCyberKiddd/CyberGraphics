#pragma once

#include <cyber/core/types.h>

#include <atomic>
#include <cstdarg>

namespace cyber::core
{

enum class LogLevel : u8
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
    Off,
};

enum class LogChannel : u8
{
    Core,
    Platform,
    Rhi,
    Renderer,
    Assets,
    Shaders,
    Jobs,
    Stability,
    Count,
};

inline constexpr usize kLogChannelCount = static_cast<usize>(LogChannel::Count);
inline constexpr usize kMaxLogMessageLength = 512;
inline constexpr usize kMaxLogSinks = 8;

const char* LogLevelName(LogLevel level) noexcept;
const char* LogChannelName(LogChannel channel) noexcept;

// One bounded log message. The text buffer is fixed so formatting cannot
// allocate. A truncated message still has a null terminator.
struct LogRecord
{
    LogLevel level = LogLevel::Info;
    LogChannel channel = LogChannel::Core;
    const char* file = nullptr;
    int line = 0;
    usize length = 0;
    bool truncated = false;
    char message[kMaxLogMessageLength]{};
};

// Sinks are non-owning and called synchronously by the logger. A sink must not
// log through the same logger; the logger lock is held while Write runs.
class LogSink
{
public:
    virtual ~LogSink() = default;

    virtual void Write(const LogRecord& record) noexcept = 0;
    virtual void Flush() noexcept {}
};

// A bounded, thread-safe, allocation-free logger. Thresholds are atomics; the
// sink table is protected by a small spinlock. Message formatting happens on
// the caller's stack before the lock is taken.
class Logger
{
public:
    Logger() noexcept;
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    bool AddSink(LogSink* sink) noexcept;
    bool RemoveSink(LogSink* sink) noexcept;

    void SetLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel Level() const noexcept;

    void SetChannelLevel(LogChannel channel, LogLevel level) noexcept;
    void ClearChannelLevel(LogChannel channel) noexcept;
    [[nodiscard]] LogLevel ChannelLevel(LogChannel channel) const noexcept;

    [[nodiscard]] bool IsEnabled(LogChannel channel, LogLevel level) const noexcept;

    void Log(LogChannel channel, LogLevel level, const char* file, int line, const char* format,
             ...) noexcept;

    void VLog(LogChannel channel, LogLevel level, const char* file, int line, const char* format,
              std::va_list args) noexcept;

    void Flush() noexcept;

private:
    void Lock() noexcept;
    void Unlock() noexcept;

    std::atomic<LogLevel> m_level{LogLevel::Info};
    std::atomic<LogLevel> m_channelLevels[kLogChannelCount]{};
    std::atomic<bool> m_channelOverridden[kLogChannelCount]{};
    LogSink* m_sinks[kMaxLogSinks]{};
    usize m_sinkCount = 0;
    mutable std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
};

// The process-wide logger used by CG_LOG. It starts with no sinks; the host
// installs a console sink or a bridge to its own logging system.
Logger& DefaultLogger() noexcept;

}  // namespace cyber::core

#define CG_LOG(level, channel, ...)                                                                \
    do                                                                                             \
    {                                                                                              \
        ::cyber::core::Logger& cgLogger = ::cyber::core::DefaultLogger();                          \
        if (cgLogger.IsEnabled(::cyber::core::LogChannel::channel,                                 \
                               ::cyber::core::LogLevel::level))                                    \
        {                                                                                          \
            cgLogger.Log(::cyber::core::LogChannel::channel, ::cyber::core::LogLevel::level,       \
                         __FILE__, __LINE__, __VA_ARGS__);                                         \
        }                                                                                          \
    } while (false)
