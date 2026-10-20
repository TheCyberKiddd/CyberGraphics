#include <cyber/core/build_config.h>
#include <cyber/core/error.h>
#include <cyber/core/hash.h>
#include <cyber/core/log.h>
#include <cyber/core/result.h>
#include <cyber/core/version.h>

#include <cstring>
#include <string_view>

int main()
{
    using namespace cyber::core;

    const Version version = VersionNumber();
    if (version.major != kVersionMajor)
    {
        return 1;
    }

    const char* text = VersionString();
    if (text == nullptr)
    {
        return 2;
    }

    if (std::strcmp(text, kVersionString) != 0)
    {
        return 3;
    }

    if (BuildConfig::kVersion != std::string_view(kVersionString))
    {
        return 4;
    }

    auto result = Result<int, Error>::Ok(21);
    if (!result.IsOk() || result.Value() != 21)
    {
        return 5;
    }

    auto failed = Result<void, Error>::Err(Error{ErrorCode::NotFound, "missing"});
    if (!failed.IsError() || failed.Error().code != ErrorCode::NotFound)
    {
        return 6;
    }

    if (HashString("abc") != 0x44BC2CF5AD770999ULL)
    {
        return 7;
    }

    Logger logger;
    logger.SetLevel(LogLevel::Info);
    if (!logger.IsEnabled(LogChannel::Core, LogLevel::Info))
    {
        return 8;
    }

    return 0;
}
