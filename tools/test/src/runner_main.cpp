#include <cyber/test/test.h>

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string_view> arguments;
    if (argc > 1)
    {
        arguments.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index)
        {
            arguments.emplace_back(argv[index]);
        }
    }

    const cyber::test::ParsedOptions parsed = cyber::test::ParseOptions(arguments);
    if (!parsed.ok)
    {
        std::cerr << "test runner: " << parsed.error << '\n';
        cyber::test::PrintUsage();
        return 2;
    }

    if (parsed.options.help)
    {
        cyber::test::PrintUsage();
        return 0;
    }

    return cyber::test::RunAll(parsed.options);
}
