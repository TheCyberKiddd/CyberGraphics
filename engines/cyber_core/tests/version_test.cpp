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
    if (version.minor != kVersionMinor)
    {
        return 2;
    }
    if (version.patch != kVersionPatch)
    {
        return 3;
    }

    const char* text = VersionString();
    if (text == nullptr)
    {
        return 4;
    }

    if (std::strcmp(text, kVersionString) != 0)
    {
        return 5;
    }

    return 0;
}
