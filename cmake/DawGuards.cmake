# Build-time enforcement of hygiene rule 3: business logic never depends on
# display or on the audio framework.
include_guard(GLOBAL)

# Fails configuration if `target` links (directly) to anything that looks like
# JUCE, Tracktion or a UI target.
function(daw_forbid_ui_dependencies target)
    get_target_property(libs ${target} LINK_LIBRARIES)
    if(NOT libs)
        return()
    endif()
    foreach(lib IN LISTS libs)
        if(lib MATCHES "^(juce::|tracktion::|juce_|tracktion_|daw_ui|daw_app)")
            message(FATAL_ERROR
                "Hygiene rule 3 violated: '${target}' must stay free of display and "
                "framework code, but links to '${lib}'.")
        endif()
    endforeach()
endfunction()
