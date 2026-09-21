# Warning flags for project-owned targets only. Never applied to JUCE,
# Tracktion or plugin SDK sources.
include_guard(GLOBAL)

# Tells MSVC that our sources are UTF-8 and that its output should be too.
#
# Separate from daw_set_warnings because daw_app and the engine test binary
# compile JUCE and Tracktion sources inside themselves: they cannot take our
# warning flags, but they must take this one. Without it MSVC reads an accented
# source file in the system code page and writes those bytes out, JUCE reads
# them back as UTF-8, and every accent in the interface turns to noise. That is
# why the interface had no accents at all until now -- they had been taken out
# rather than made to work.
function(daw_use_utf8_sources target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

function(daw_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(DAW_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)
        if(DAW_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
