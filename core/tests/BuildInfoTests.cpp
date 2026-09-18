#include "daw/domain/BuildInfo.h"

#include <doctest/doctest.h>

TEST_CASE("The domain reports the version CMake gave it")
{
    CHECK_FALSE(daw::domain::versionString().empty());
}
