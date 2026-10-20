#pragma once

#include <cyber/core/log.h>

#include <atomic>
#include <cstdio>

namespace cyber::core
{

// Formats a log record without allocating. The format is:
//   [INFO ] [core] path/file.cpp:42: message
// Returns the number of characters the full line would need, excluding the
// null terminator. A null buffer or zero capacity performs no write.
usize FormatLogLine(char* buffer, usize capacity, const LogRecord& record) noexcept;

// Writes one line per record to a std::FILE*. The default stream is stderr.
// The sink is safe to call from multiple loggers; writes are serialized.
class ConsoleSink final : public LogSink
{
public:
    explicit ConsoleSink(std::FILE* stream = stderr) noexcept;
    ~ConsoleSink() override = default;

    void Write(const LogRecord& record) noexcept override;
    void Flush() noexcept override;

private:
    std::FILE* m_stream;
    std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
};

// Forwards records to a plain function pointer. The callback runs under the
// logger lock, so it must be short and must not log through the same logger.
class CallbackSink final : public LogSink
{
public:
    using Callback = void (*)(void* userData, const LogRecord& record) noexcept;

    explicit CallbackSink(Callback callback, void* userData = nullptr) noexcept;
    ~CallbackSink() override = default;

    void Write(const LogRecord& record) noexcept override;

private:
    Callback m_callback;
    void* m_userData;
};

}  // namespace cyber::core
