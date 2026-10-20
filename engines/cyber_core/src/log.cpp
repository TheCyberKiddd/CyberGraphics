#include <cyber/core/log.h>

#include <cstdio>
#include <cstring>

namespace cyber::core
{

const char* LogLevelName(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    case LogLevel::Off:
        return "OFF";
    }
    return "UNKNOWN";
}

const char* LogChannelName(LogChannel channel) noexcept
{
    switch (channel)
    {
    case LogChannel::Core:
        return "core";
    case LogChannel::Platform:
        return "platform";
    case LogChannel::Rhi:
        return "rhi";
    case LogChannel::Renderer:
        return "renderer";
    case LogChannel::Assets:
        return "assets";
    case LogChannel::Shaders:
        return "shaders";
    case LogChannel::Jobs:
        return "jobs";
    case LogChannel::Stability:
        return "stability";
    case LogChannel::Count:
        return "unknown";
    }
    return "unknown";
}

Logger::Logger() noexcept
{
    for (usize index = 0; index < kLogChannelCount; ++index)
    {
        m_channelLevels[index].store(LogLevel::Trace, std::memory_order_relaxed);
        m_channelOverridden[index].store(false, std::memory_order_relaxed);
    }
}

bool Logger::AddSink(LogSink* sink) noexcept
{
    if (sink == nullptr)
    {
        return false;
    }

    Lock();
    bool added = false;
    if (m_sinkCount < kMaxLogSinks)
    {
        bool duplicate = false;
        for (usize index = 0; index < m_sinkCount; ++index)
        {
            if (m_sinks[index] == sink)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
        {
            m_sinks[m_sinkCount] = sink;
            ++m_sinkCount;
            added = true;
        }
    }
    Unlock();
    return added;
}

bool Logger::RemoveSink(LogSink* sink) noexcept
{
    if (sink == nullptr)
    {
        return false;
    }

    Lock();
    bool removed = false;
    for (usize index = 0; index < m_sinkCount; ++index)
    {
        if (m_sinks[index] == sink)
        {
            for (usize moveIndex = index + 1; moveIndex < m_sinkCount; ++moveIndex)
            {
                m_sinks[moveIndex - 1] = m_sinks[moveIndex];
            }
            --m_sinkCount;
            m_sinks[m_sinkCount] = nullptr;
            removed = true;
            break;
        }
    }
    Unlock();
    return removed;
}

void Logger::SetLevel(LogLevel level) noexcept
{
    m_level.store(level, std::memory_order_release);
}

LogLevel Logger::Level() const noexcept
{
    return m_level.load(std::memory_order_acquire);
}

void Logger::SetChannelLevel(LogChannel channel, LogLevel level) noexcept
{
    const usize index = static_cast<usize>(channel);
    if (index >= kLogChannelCount)
    {
        return;
    }
    m_channelLevels[index].store(level, std::memory_order_release);
    m_channelOverridden[index].store(true, std::memory_order_release);
}

void Logger::ClearChannelLevel(LogChannel channel) noexcept
{
    const usize index = static_cast<usize>(channel);
    if (index >= kLogChannelCount)
    {
        return;
    }
    m_channelOverridden[index].store(false, std::memory_order_release);
}

LogLevel Logger::ChannelLevel(LogChannel channel) const noexcept
{
    const usize index = static_cast<usize>(channel);
    if (index >= kLogChannelCount)
    {
        return LogLevel::Off;
    }
    if (m_channelOverridden[index].load(std::memory_order_acquire))
    {
        return m_channelLevels[index].load(std::memory_order_acquire);
    }
    return m_level.load(std::memory_order_acquire);
}

bool Logger::IsEnabled(LogChannel channel, LogLevel level) const noexcept
{
    if (level == LogLevel::Off)
    {
        return false;
    }
    const LogLevel threshold = ChannelLevel(channel);
    if (threshold == LogLevel::Off)
    {
        return false;
    }
    return level >= threshold;
}

void Logger::Log(LogChannel channel, LogLevel level, const char* file, int line, const char* format,
                 ...) noexcept
{
    std::va_list args;
    va_start(args, format);
    VLog(channel, level, file, line, format, args);
    va_end(args);
}

void Logger::VLog(LogChannel channel, LogLevel level, const char* file, int line,
                  const char* format, std::va_list args) noexcept
{
    if (!IsEnabled(channel, level))
    {
        return;
    }

    LogRecord record;
    record.level = level;
    record.channel = channel;
    record.file = file;
    record.line = line;

    if (format == nullptr)
    {
        constexpr const char* kNullFormat = "<null format>";
        record.length = std::strlen(kNullFormat);
        std::memcpy(record.message, kNullFormat, record.length + 1);
    }
    else
    {
        const int written = std::vsnprintf(record.message, sizeof(record.message), format, args);
        if (written < 0)
        {
            constexpr const char* kFormatError = "<log format error>";
            record.length = std::strlen(kFormatError);
            std::memcpy(record.message, kFormatError, record.length + 1);
        }
        else
        {
            record.length = static_cast<usize>(written);
            if (record.length >= kMaxLogMessageLength)
            {
                record.length = kMaxLogMessageLength - 1;
                record.truncated = true;
            }
        }
    }

    Lock();
    for (usize index = 0; index < m_sinkCount; ++index)
    {
        m_sinks[index]->Write(record);
    }
    Unlock();
}

void Logger::Flush() noexcept
{
    Lock();
    for (usize index = 0; index < m_sinkCount; ++index)
    {
        m_sinks[index]->Flush();
    }
    Unlock();
}

void Logger::Lock() noexcept
{
    while (m_lock.test_and_set(std::memory_order_acquire))
    {
    }
}

void Logger::Unlock() noexcept
{
    m_lock.clear(std::memory_order_release);
}

Logger& DefaultLogger() noexcept
{
    static Logger logger;
    return logger;
}

}  // namespace cyber::core
