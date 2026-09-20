# Third-party code fetched at configure time (pinned by archive hash, not by tag).
#
# Three dependencies, all invisible from the outside:
#   nlohmann/json  used only inside core/domain/src/serialization/Json.cpp
#   doctest        used only by core/tests
#   SQLite         used only inside core/persistence/src
#
# Unlike external/ (git submodules for JUCE, Tracktion, CLAP, VST3), these are
# small enough to download, so the `domain-only` preset keeps working without
# submodules. The archives are pinned by SHA-256: a tag moving upstream is a
# configure error, not a silent change.
include_guard(GLOBAL)

include(FetchContent)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

if(DAW_BUILD_TESTS)
    FetchContent_Declare(doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
        URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM)
    set(DOCTEST_WITH_TESTS OFF CACHE INTERNAL "")
    set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
    FetchContent_MakeAvailable(doctest)

    # Provides doctest_discover_tests(): one ctest entry per TEST_CASE.
    list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
    include(doctest)
endif()

# --- SQLite ------------------------------------------------------------------
# The official amalgamation: one C file, no build system of its own, pinned by
# the SHA-256 of the archive. A tag moving upstream is a configure error.
#
# It is compiled as our own static library rather than linked from the system,
# because a project file written by one SQLite and read by another is a
# compatibility question this project should not have to ask on three machines.
FetchContent_Declare(sqlite3
    URL https://sqlite.org/2026/sqlite-amalgamation-3530400.zip
    URL_HASH SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite3)

add_library(daw_sqlite3 STATIC "${sqlite3_SOURCE_DIR}/sqlite3.c")
add_library(daw::sqlite3 ALIAS daw_sqlite3)
target_include_directories(daw_sqlite3 SYSTEM PUBLIC "${sqlite3_SOURCE_DIR}")

# Compile-time configuration, and every line of it is a decision:
#   THREADSAFE=1        the bus thread writes while a reader may read.
#   DQS=0               a double-quoted string is an identifier, never a
#                       string literal: a typo in a column name has to fail.
#   DEFAULT_FOREIGN_KEYS the schema means what it says.
#   OMIT_LOAD_EXTENSION  a project file can never ask to load a library.
#   OMIT_DEPRECATED      none of it is used, and it is dead code in the binary.
target_compile_definitions(daw_sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_USE_URI=0)

# Third-party code never gets our warning flags, and never fails our build on a
# warning of its own.
if(MSVC)
    target_compile_options(daw_sqlite3 PRIVATE /W0)
else()
    target_compile_options(daw_sqlite3 PRIVATE -w)
endif()

set_target_properties(daw_sqlite3 PROPERTIES FOLDER "external")
