#include <cyber/core/log.h>
#include <cyber/core/log_sinks.h>
#include <cyber/test/test.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace
{

class RecordingSink final : public cyber::core::LogSink
{
public:
    RecordingSink()
    {
        records.reserve(16);
    }

    void Write(const cyber::core::LogRecord& record) noexcept override
    {
        records.push_back(record);
    }

    std::vector<cyber::core::LogRecord> records;
};

class CountingSink final : public cyber::core::LogSink
{
public:
    void Write(const cyber::core::LogRecord&) noexcept override
    {
        count.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<int> count{0};
};

struct CallbackState
{
    int count = 0;
    cyber::core::LogLevel lastLevel = cyber::core::LogLevel::Off;
};

void OnLog(void* userData, const cyber::core::LogRecord& record) noexcept
{
    auto* state = static_cast<CallbackState*>(userData);
    ++state->count;
    state->lastLevel = record.level;
}

}  // namespace

CG_TEST(log_level_and_channel_names)
{
    CG_EXPECT_STREQ(cyber::core::LogLevelName(cyber::core::LogLevel::Trace), "TRACE");
    CG_EXPECT_STREQ(cyber::core::LogLevelName(cyber::core::LogLevel::Debug), "DEBUG");
    CG_EXPECT_STREQ(cyber::core::LogLevelName(cyber::core::LogLevel::Info), "INFO");
    CG_EXPECT_STREQ(cyber::core::LogLevelName(cyber::core::LogLevel::Warning), "WARN");
    CG_EXPECT_STREQ(cyber::core::LogLevelName(cyber::core::LogLevel::Error), "ERROR");
    CG_EXPECT_STREQ(cyber::core::LogLevelName(cyber::core::LogLevel::Fatal), "FATAL");
    CG_EXPECT_STREQ(cyber::core::LogLevelName(cyber::core::LogLevel::Off), "OFF");

    CG_EXPECT_STREQ(cyber::core::LogChannelName(cyber::core::LogChannel::Core), "core");
    CG_EXPECT_STREQ(cyber::core::LogChannelName(cyber::core::LogChannel::Platform), "platform");
    CG_EXPECT_STREQ(cyber::core::LogChannelName(cyber::core::LogChannel::Rhi), "rhi");
    CG_EXPECT_STREQ(cyber::core::LogChannelName(cyber::core::LogChannel::Renderer), "renderer");
    CG_EXPECT_STREQ(cyber::core::LogChannelName(cyber::core::LogChannel::Assets), "assets");
    CG_EXPECT_STREQ(cyber::core::LogChannelName(cyber::core::LogChannel::Shaders), "shaders");
    CG_EXPECT_STREQ(cyber::core::LogChannelName(cyber::core::LogChannel::Jobs), "jobs");
    CG_EXPECT_STREQ(cyber::core::LogChannelName(cyber::core::LogChannel::Stability), "stability");
}

CG_TEST(log_filters_by_level)
{
    cyber::core::Logger logger;
    RecordingSink sink;
    CG_EXPECT_TRUE(logger.AddSink(&sink));

    logger.SetLevel(cyber::core::LogLevel::Warning);
    CG_EXPECT_EQ(logger.Level(), cyber::core::LogLevel::Warning);
    CG_EXPECT_FALSE(logger.IsEnabled(cyber::core::LogChannel::Core, cyber::core::LogLevel::Info));
    CG_EXPECT_TRUE(logger.IsEnabled(cyber::core::LogChannel::Core, cyber::core::LogLevel::Warning));

    logger.Log(cyber::core::LogChannel::Core, cyber::core::LogLevel::Info, __FILE__, __LINE__,
               "hidden");
    CG_EXPECT_TRUE(sink.records.empty());

    logger.Log(cyber::core::LogChannel::Core, cyber::core::LogLevel::Warning, __FILE__, __LINE__,
               "visible %d", 3);
    CG_EXPECT_EQ(sink.records.size(), std::size_t{1});
    CG_EXPECT_EQ(sink.records[0].level, cyber::core::LogLevel::Warning);
    CG_EXPECT_STREQ(sink.records[0].message, "visible 3");
    CG_EXPECT_EQ(sink.records[0].length, std::size_t{9});
}

CG_TEST(log_channel_override)
{
    cyber::core::Logger logger;
    RecordingSink sink;
    CG_EXPECT_TRUE(logger.AddSink(&sink));

    logger.SetLevel(cyber::core::LogLevel::Trace);
    logger.SetChannelLevel(cyber::core::LogChannel::Rhi, cyber::core::LogLevel::Error);
    CG_EXPECT_EQ(logger.ChannelLevel(cyber::core::LogChannel::Rhi), cyber::core::LogLevel::Error);
    CG_EXPECT_FALSE(logger.IsEnabled(cyber::core::LogChannel::Rhi, cyber::core::LogLevel::Info));
    CG_EXPECT_TRUE(logger.IsEnabled(cyber::core::LogChannel::Rhi, cyber::core::LogLevel::Error));
    CG_EXPECT_TRUE(logger.IsEnabled(cyber::core::LogChannel::Core, cyber::core::LogLevel::Info));

    logger.Log(cyber::core::LogChannel::Rhi, cyber::core::LogLevel::Info, __FILE__, __LINE__,
               "hidden");
    logger.Log(cyber::core::LogChannel::Rhi, cyber::core::LogLevel::Error, __FILE__, __LINE__,
               "visible");
    logger.Log(cyber::core::LogChannel::Core, cyber::core::LogLevel::Info, __FILE__, __LINE__,
               "core");
    CG_EXPECT_EQ(sink.records.size(), std::size_t{2});

    logger.ClearChannelLevel(cyber::core::LogChannel::Rhi);
    CG_EXPECT_EQ(logger.ChannelLevel(cyber::core::LogChannel::Rhi), cyber::core::LogLevel::Trace);
    logger.Log(cyber::core::LogChannel::Rhi, cyber::core::LogLevel::Info, __FILE__, __LINE__,
               "now");
    CG_EXPECT_EQ(sink.records.size(), std::size_t{3});
}

CG_TEST(log_sink_registration)
{
    cyber::core::Logger logger;
    std::array<RecordingSink, cyber::core::kMaxLogSinks> sinks;

    CG_EXPECT_FALSE(logger.AddSink(nullptr));
    for (RecordingSink& sink : sinks)
    {
        CG_EXPECT_TRUE(logger.AddSink(&sink));
    }
    CG_EXPECT_FALSE(logger.AddSink(&sinks[0]));
    CG_EXPECT_FALSE(logger.AddSink(&sinks[0]));

    CG_EXPECT_FALSE(logger.RemoveSink(nullptr));
    CG_EXPECT_TRUE(logger.RemoveSink(&sinks[2]));
    CG_EXPECT_FALSE(logger.RemoveSink(&sinks[2]));
    CG_EXPECT_TRUE(logger.RemoveSink(&sinks[0]));
}

CG_TEST(log_message_truncation)
{
    cyber::core::Logger logger;
    RecordingSink sink;
    CG_EXPECT_TRUE(logger.AddSink(&sink));

    const std::string longMessage(cyber::core::kMaxLogMessageLength * 2, 'x');
    logger.Log(cyber::core::LogChannel::Core, cyber::core::LogLevel::Info, __FILE__, __LINE__, "%s",
               longMessage.c_str());

    CG_REQUIRE(sink.records.size() == std::size_t{1});
    const cyber::core::LogRecord& record = sink.records[0];
    CG_EXPECT_TRUE(record.truncated);
    CG_EXPECT_EQ(record.length, cyber::core::kMaxLogMessageLength - 1);
    CG_EXPECT_EQ(record.message[record.length], '\0');
}

CG_TEST(log_format_line)
{
    cyber::core::LogRecord record;
    record.level = cyber::core::LogLevel::Warning;
    record.channel = cyber::core::LogChannel::Rhi;
    record.file = "engine/rhi/device.cpp";
    record.line = 9;
    record.length = 5;
    const char* text = "hello";
    for (std::size_t index = 0; index < record.length; ++index)
    {
        record.message[index] = text[index];
    }
    record.message[record.length] = '\0';

    char buffer[512];
    const auto needed = cyber::core::FormatLogLine(buffer, sizeof(buffer), record);
    CG_EXPECT_TRUE(needed > 0);
    CG_EXPECT_TRUE(std::string(buffer).find("[WARN ]") != std::string::npos);
    CG_EXPECT_TRUE(std::string(buffer).find("[rhi") != std::string::npos);
    CG_EXPECT_TRUE(std::string(buffer).find("engine/rhi/device.cpp:9") != std::string::npos);
    CG_EXPECT_TRUE(std::string(buffer).find("hello") != std::string::npos);
}

CG_TEST(log_callback_sink)
{
    cyber::core::Logger logger;
    CallbackState state;
    cyber::core::CallbackSink sink(&OnLog, &state);
    CG_EXPECT_TRUE(logger.AddSink(&sink));

    logger.SetLevel(cyber::core::LogLevel::Trace);
    logger.Log(cyber::core::LogChannel::Assets, cyber::core::LogLevel::Error, __FILE__, __LINE__,
               "asset failed");

    CG_EXPECT_EQ(state.count, 1);
    CG_EXPECT_EQ(state.lastLevel, cyber::core::LogLevel::Error);
}

CG_TEST(log_default_macro)
{
    cyber::core::Logger& logger = cyber::core::DefaultLogger();
    RecordingSink sink;
    logger.SetLevel(cyber::core::LogLevel::Info);
    CG_EXPECT_TRUE(logger.AddSink(&sink));

    CG_LOG(Info, Core, "macro value %d", 11);
    CG_EXPECT_EQ(sink.records.size(), std::size_t{1});
    CG_EXPECT_STREQ(sink.records[0].message, "macro value 11");

    logger.RemoveSink(&sink);
}

CG_TEST(log_concurrent_writers)
{
    cyber::core::Logger logger;
    CountingSink sink;
    CG_EXPECT_TRUE(logger.AddSink(&sink));
    logger.SetLevel(cyber::core::LogLevel::Info);

    constexpr int kThreadCount = 4;
    constexpr int kMessagesPerThread = 100;
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
    {
        threads.emplace_back(
            [&logger, threadIndex]()
            {
                for (int messageIndex = 0; messageIndex < kMessagesPerThread; ++messageIndex)
                {
                    logger.Log(cyber::core::LogChannel::Jobs, cyber::core::LogLevel::Info, __FILE__,
                               __LINE__, "thread %d message %d", threadIndex, messageIndex);
                }
            });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    CG_EXPECT_EQ(sink.count.load(), kThreadCount * kMessagesPerThread);
}
