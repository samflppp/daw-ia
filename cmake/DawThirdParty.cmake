# Third-party code fetched at configure time (pinned by archive hash, not by tag).
#
# Both dependencies are header-only and stay invisible from the outside:
#   nlohmann/json  used only inside core/domain/src/serialization/Json.cpp
#   doctest        used only by core/tests
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
