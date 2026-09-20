# persistence/ — the project on disk: SQLite journal and project folder.
#
# Depends on domain/ and on SQLite, and on nothing else. No JUCE, no Tracktion:
# the persistence tests run in the `domain-only` preset, without an audio
# device and without submodules, which is what keeps them runnable on every
# save.
add_library(daw_persistence STATIC
    src/Database.cpp
    src/ProjectFolder.cpp
    src/ProjectStore.cpp)
add_library(daw::persistence ALIAS daw_persistence)

target_include_directories(daw_persistence PUBLIC include)
target_compile_features(daw_persistence PUBLIC cxx_std_20)

# SQLite is PRIVATE: sqlite3.h appears in no header of ours, so no other module
# inherits it and Database.h forward-declares the two handles it needs.
target_link_libraries(daw_persistence PUBLIC daw::domain PRIVATE daw::sqlite3)

daw_set_warnings(daw_persistence)

if(DAW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
