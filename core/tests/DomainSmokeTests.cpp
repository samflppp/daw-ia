#include "daw/domain/BuildInfo.h"

#include <cstdio>
#include <cstdlib>

int main()
{
    const auto version = daw::domain::versionString();
    if (version.empty())
    {
        std::fputs("FAIL: versionString() is empty\n", stderr);
        return EXIT_FAILURE;
    }

    std::printf("OK: daw core %.*s\n", static_cast<int>(version.size()), version.data());
    return EXIT_SUCCESS;
}
