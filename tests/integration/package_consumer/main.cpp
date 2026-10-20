#include <cyber/core/version.h>

#include <cstring>

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

    return 0;
}
