#include <cyber/test/test.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cyber::test
{
namespace
{

struct Failure
{
    std::string message;
    const char* file = nullptr;
    int line = 0;
};

struct Registry
{
    std::vector<TestCase> tests;
};

Registry& GetRegistry()
{
    static Registry registry;
    return registry;
}

thread_local std::vector<Failure>* g_currentFailures = nullptr;
std::uint64_t g_seed = 0;

struct RunResult
{
    TestCase test;
    std::string displayName;
    std::vector<Failure> failures;
    double milliseconds = 0.0;
};

std::string SingleLine(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const char character : text)
    {
        switch (character)
        {
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

std::string FormatSeconds(double milliseconds)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << (milliseconds / 1000.0);
    return stream.str();
}

std::string DisplayName(const TestCase& test, std::size_t repeatIndex, std::uint32_t repeatCount)
{
    std::string name = test.name != nullptr ? test.name : "<unnamed>";
    if (repeatCount > 1)
    {
        name += " [repeat ";
        name += std::to_string(repeatIndex + 1);
        name += "/";
        name += std::to_string(repeatCount);
        name += "]";
    }
    return name;
}

template <typename T>
bool ParseUnsigned(std::string_view text, T& value) noexcept
{
    if (text.empty())
    {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

ParsedOptions MakeError(std::string message)
{
    ParsedOptions parsed;
    parsed.ok = false;
    parsed.error = std::move(message);
    return parsed;
}

std::vector<Failure>* CurrentFailures()
{
    if (g_currentFailures == nullptr)
    {
        std::cerr << "test runner: assertion outside a running test\n";
        std::abort();
    }
    return g_currentFailures;
}

void AddFailure(std::string message, const char* file, int line)
{
    CurrentFailures()->push_back(Failure{std::move(message), file, line});
}

RunResult RunOne(const TestCase& test, std::size_t repeatIndex, std::uint32_t repeatCount)
{
    RunResult result;
    result.test = test;
    result.displayName = DisplayName(test, repeatIndex, repeatCount);

    const auto start = std::chrono::steady_clock::now();
    g_currentFailures = &result.failures;
    try
    {
        test.function();
    }
    catch (const std::exception& exception)
    {
        result.failures.push_back(
            Failure{std::string("unhandled exception: ") + exception.what(), test.file, test.line});
    }
    catch (...)
    {
        result.failures.push_back(
            Failure{"unhandled non-standard exception", test.file, test.line});
    }
    g_currentFailures = nullptr;

    const auto finish = std::chrono::steady_clock::now();
    result.milliseconds = std::chrono::duration<double, std::milli>(finish - start).count();
    return result;
}

void PrintConsole(const std::vector<RunResult>& results)
{
    std::size_t failedTests = 0;
    for (const RunResult& result : results)
    {
        std::cout << "[ RUN      ] " << result.displayName << '\n';
        if (result.failures.empty())
        {
            std::cout << "[       OK ] " << result.displayName << " (" << std::fixed
                      << std::setprecision(3) << result.milliseconds << " ms)\n";
        }
        else
        {
            ++failedTests;
            std::cout << "[  FAILED  ] " << result.displayName << " (" << std::fixed
                      << std::setprecision(3) << result.milliseconds << " ms)\n";
            for (const Failure& failure : result.failures)
            {
                std::cout << "  " << (failure.file != nullptr ? failure.file : "<unknown>") << ":"
                          << failure.line << ": " << failure.message << '\n';
            }
        }
    }

    std::cout << "[==========] " << results.size() << " test(s) ran.\n";
    std::cout << "[  PASSED  ] " << (results.size() - failedTests) << " test(s).\n";
    if (failedTests != 0)
    {
        std::cout << "[  FAILED  ] " << failedTests << " test(s).\n";
    }
}

void PrintTap(const std::vector<RunResult>& results)
{
    std::cout << "TAP version 13\n";
    std::cout << "1.." << results.size() << '\n';

    std::size_t index = 1;
    for (const RunResult& result : results)
    {
        if (result.failures.empty())
        {
            std::cout << "ok " << index << " - " << result.displayName << '\n';
        }
        else
        {
            std::cout << "not ok " << index << " - " << result.displayName << '\n';
            for (const Failure& failure : result.failures)
            {
                std::cout << "  # " << (failure.file != nullptr ? failure.file : "<unknown>") << ":"
                          << failure.line << ": " << SingleLine(failure.message) << '\n';
            }
        }
        ++index;
    }
}

void PrintJunit(const std::vector<RunResult>& results)
{
    std::size_t failedTests = 0;
    double totalMilliseconds = 0.0;
    for (const RunResult& result : results)
    {
        if (!result.failures.empty())
        {
            ++failedTests;
        }
        totalMilliseconds += result.milliseconds;
    }

    std::cout << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    std::cout << "<testsuites tests=\"" << results.size() << "\" failures=\"" << failedTests
              << "\" errors=\"0\" time=\"" << FormatSeconds(totalMilliseconds) << "\">\n";
    std::cout << "  <testsuite name=\"cyber\" tests=\"" << results.size() << "\" failures=\""
              << failedTests << "\" errors=\"0\" skipped=\"0\" time=\""
              << FormatSeconds(totalMilliseconds) << "\">\n";

    for (const RunResult& result : results)
    {
        std::cout << "    <testcase classname=\"cyber\" name=\"" << XmlEscape(result.displayName)
                  << "\" file=\""
                  << XmlEscape(result.test.file != nullptr ? result.test.file : "<unknown>")
                  << "\" line=\"" << result.test.line << "\" time=\""
                  << FormatSeconds(result.milliseconds) << "\">\n";
        for (const Failure& failure : result.failures)
        {
            std::cout << "      <failure message=\"" << XmlEscape(SingleLine(failure.message))
                      << "\">" << XmlEscape(failure.file != nullptr ? failure.file : "<unknown>")
                      << ":" << failure.line << ": " << XmlEscape(failure.message)
                      << "</failure>\n";
        }
        std::cout << "    </testcase>\n";
    }

    std::cout << "  </testsuite>\n";
    std::cout << "</testsuites>\n";
}

}  // namespace

void RegisterTest(const char* name, const char* file, int line, TestFunction function)
{
    if (name == nullptr || function == nullptr)
    {
        std::cerr << "test runner: invalid test registration\n";
        std::abort();
    }
    GetRegistry().tests.push_back(TestCase{name, file, line, function});
}

Registrar::Registrar(const char* name, const char* file, int line, TestFunction function)
{
    RegisterTest(name, file, line, function);
}

std::size_t TestCount() noexcept
{
    return GetRegistry().tests.size();
}

const TestCase* TestAt(std::size_t index) noexcept
{
    const Registry& registry = GetRegistry();
    if (index >= registry.tests.size())
    {
        return nullptr;
    }
    return &registry.tests[index];
}

void Expect(bool condition, std::string_view expression, const char* file, int line)
{
    if (condition)
    {
        return;
    }
    AddFailure(std::string("expectation failed: ") + std::string(expression), file, line);
}

void Fail(std::string_view message, const char* file, int line)
{
    AddFailure(std::string(message), file, line);
}

void Require(bool condition, std::string_view expression, const char* file, int line)
{
    if (condition)
    {
        return;
    }
    AddFailure(std::string("requirement failed: ") + std::string(expression), file, line);
}

bool MatchesFilter(std::string_view name, std::string_view filter) noexcept
{
    return filter.empty() || name.find(filter) != std::string_view::npos;
}

std::string XmlEscape(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text)
    {
        switch (character)
        {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&apos;";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

std::uint64_t Seed() noexcept
{
    return g_seed;
}

bool NearEquals(double actual, double expected, double tolerance) noexcept
{
    if (tolerance < 0.0)
    {
        return false;
    }
    if (actual == expected)
    {
        return true;
    }
    return std::abs(actual - expected) <= tolerance;
}

ParsedOptions ParseOptions(std::span<const std::string_view> arguments)
{
    ParsedOptions parsed;

    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];

        if (argument == "--help" || argument == "-h")
        {
            parsed.options.help = true;
            continue;
        }
        if (argument == "--list")
        {
            parsed.options.list = true;
            continue;
        }
        if (argument == "--shuffle")
        {
            parsed.options.shuffle = true;
            continue;
        }
        if (argument == "--filter")
        {
            if (index + 1 >= arguments.size())
            {
                return MakeError("--filter requires a value");
            }
            parsed.options.filter = std::string(arguments[++index]);
            continue;
        }
        if (argument.starts_with("--filter="))
        {
            parsed.options.filter = std::string(argument.substr(9));
            continue;
        }
        if (argument == "--seed")
        {
            if (index + 1 >= arguments.size())
            {
                return MakeError("--seed requires a value");
            }
            if (!ParseUnsigned(arguments[++index], parsed.options.seed))
            {
                return MakeError("--seed value is not an unsigned integer");
            }
            continue;
        }
        if (argument.starts_with("--seed="))
        {
            if (!ParseUnsigned(argument.substr(7), parsed.options.seed))
            {
                return MakeError("--seed value is not an unsigned integer");
            }
            continue;
        }
        if (argument == "--repeat")
        {
            if (index + 1 >= arguments.size())
            {
                return MakeError("--repeat requires a value");
            }
            if (!ParseUnsigned(arguments[++index], parsed.options.repeat))
            {
                return MakeError("--repeat value is not an unsigned integer");
            }
            continue;
        }
        if (argument.starts_with("--repeat="))
        {
            if (!ParseUnsigned(argument.substr(9), parsed.options.repeat))
            {
                return MakeError("--repeat value is not an unsigned integer");
            }
            continue;
        }
        if (argument == "--format")
        {
            if (index + 1 >= arguments.size())
            {
                return MakeError("--format requires a value");
            }
            const std::string_view value = arguments[++index];
            if (value == "console")
            {
                parsed.options.format = Options::Format::Console;
            }
            else if (value == "tap")
            {
                parsed.options.format = Options::Format::Tap;
            }
            else if (value == "junit")
            {
                parsed.options.format = Options::Format::Junit;
            }
            else
            {
                return MakeError("unknown output format: " + std::string(value));
            }
            continue;
        }
        if (argument.starts_with("--format="))
        {
            const std::string_view value = argument.substr(9);
            if (value == "console")
            {
                parsed.options.format = Options::Format::Console;
            }
            else if (value == "tap")
            {
                parsed.options.format = Options::Format::Tap;
            }
            else if (value == "junit")
            {
                parsed.options.format = Options::Format::Junit;
            }
            else
            {
                return MakeError("unknown output format: " + std::string(value));
            }
            continue;
        }

        return MakeError("unknown argument: " + std::string(argument));
    }

    if (parsed.options.repeat == 0 || parsed.options.repeat > 1000)
    {
        return MakeError("--repeat must be between 1 and 1000");
    }

    return parsed;
}

void PrintUsage()
{
    std::cout << "usage: test-runner [options]\n"
              << "  --list              list registered tests\n"
              << "  --filter <text>     run tests whose name contains text\n"
              << "  --repeat <count>    run every selected test count times (1..1000)\n"
              << "  --seed <value>      seed exposed to tests and --shuffle\n"
              << "  --shuffle           randomize test order with --seed\n"
              << "  --format <format>   console, tap, or junit\n"
              << "  --help, -h          show this help\n";
}

int RunAll(const Options& options)
{
    const Registry& registry = GetRegistry();

    for (std::size_t first = 0; first < registry.tests.size(); ++first)
    {
        for (std::size_t second = first + 1; second < registry.tests.size(); ++second)
        {
            if (std::string_view(registry.tests[first].name) ==
                std::string_view(registry.tests[second].name))
            {
                std::cerr << "test runner: duplicate test name: " << registry.tests[first].name
                          << '\n';
                return 2;
            }
        }
    }

    std::vector<TestCase> selected;
    selected.reserve(registry.tests.size());
    for (const TestCase& test : registry.tests)
    {
        if (MatchesFilter(test.name, options.filter))
        {
            selected.push_back(test);
        }
    }

    if (options.list)
    {
        for (const TestCase& test : selected)
        {
            std::cout << test.name << " (" << (test.file != nullptr ? test.file : "<unknown>")
                      << ":" << test.line << ")\n";
        }
        return 0;
    }

    if (selected.empty())
    {
        std::cerr << "test runner: no tests matched";
        if (!options.filter.empty())
        {
            std::cerr << " filter '" << options.filter << "'";
        }
        std::cerr << '\n';
        return 2;
    }

    g_seed = options.seed;

    if (options.shuffle)
    {
        std::mt19937_64 randomEngine(options.seed);
        std::shuffle(selected.begin(), selected.end(), randomEngine);
    }

    std::vector<RunResult> results;
    results.reserve(selected.size() * options.repeat);
    for (std::uint32_t repeatIndex = 0; repeatIndex < options.repeat; ++repeatIndex)
    {
        for (const TestCase& test : selected)
        {
            results.push_back(RunOne(test, repeatIndex, options.repeat));
        }
    }

    switch (options.format)
    {
    case Options::Format::Console:
        PrintConsole(results);
        break;
    case Options::Format::Tap:
        PrintTap(results);
        break;
    case Options::Format::Junit:
        PrintJunit(results);
        break;
    }

    for (const RunResult& result : results)
    {
        if (!result.failures.empty())
        {
            return 1;
        }
    }
    return 0;
}

}  // namespace cyber::test
