// doctest supplies no main() here: this binary has two jobs. Run without
// arguments it is the test suite; run with --child it is the process a test
// launched to write a project and then die.
//
// That is the whole point of the week. A loader called twice in one process
// proves nothing about persistence: the second call can read what the first
// one left in memory, in a cache, in an open connection. Closing the process
// is the only way to be sure the bytes on disk are what carries the project.
#define DOCTEST_CONFIG_IMPLEMENT

#include "PersistenceTestSupport.h"

#include <cstdlib>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

namespace
{

int runChildScenario(int argc, char** argv)
{
    const std::string_view scenario{argv[2]};

    if (scenario == "write" && argc >= 5)
        return daw::testing::scenarios::writeSession(argv[3], argv[4]);

    if (scenario == "write-large" && argc >= 6)
        return daw::testing::scenarios::writeLargeSession(argv[3], argv[4], std::atoi(argv[5]));

    return 64; // unknown scenario: an exit code no scenario returns
}

} // namespace

int main(int argc, char** argv)
{
    daw::testing::setExecutablePath(argc > 0 ? argv[0] : nullptr);

    if (argc >= 3 && std::string_view{argv[1]} == "--child")
        return runChildScenario(argc, argv);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
