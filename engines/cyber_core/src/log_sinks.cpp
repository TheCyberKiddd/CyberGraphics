#include <cyber/core/log_sinks.h>

#include <algorithm>
#include <cstdio>

namespace cyber::core
{

usize FormatLogLine(char* buffer, usize capacity, const LogRecord& record) noexcept
{
    if (buffer == nullptr || capacity == 0)
    {
        return 0;
    }

    const char* level = LogLevelName(record.level);
    const char* channel = LogChannelName(record.channel);
    const char* file = record.file != nullptr ? record.file : "<unknown>";
    const usize messageLength = std::min(record.length, kMaxLogMessageLength - 1);

    const int written =
        std::snprintf(buffer, capacity, "[%-5s] [%-9s] %s:%d: %.*s", level, channel, file,
                      record.line, static_cast<int>(messageLength), record.message);
    return written > 0 ? static_cast<usize>(written) : 0;
}

ConsoleSink::ConsoleSink(std::FILE* stream) noexcept : m_stream(stream != nullptr ? stream : stderr)
{
}

void ConsoleSink::Write(const LogRecord& record) noexcept
{
    char line[768];
    const usize needed = FormatLogLine(line, sizeof(line), record);
    const usize written = needed < sizeof(line) ? needed : sizeof(line) - 1;

    while (m_lock.test_and_set(std::memory_order_acquire))
    {
    }
    (void)std::fwrite(line, 1, written, m_stream);
    (void)std::fputc('\n', m_stream);
    if (record.level >= LogLevel::Warning)
    {
        (void)std::fflush(m_stream);
    }
    m_lock.clear(std::memory_order_release);
}

void ConsoleSink::Flush() noexcept
{
    while (m_lock.test_and_set(std::memory_order_acquire))
    {
    }
    (void)std::fflush(m_stream);
    m_lock.clear(std::memory_order_release);
}

CallbackSink::CallbackSink(Callback callback, void* userData) noexcept
    : m_callback(callback), m_userData(userData)
{
}

void CallbackSink::Write(const LogRecord& record) noexcept
{
    if (m_callback != nullptr)
    {
        m_callback(m_userData, record);
    }
}

}  // namespace cyber::core
