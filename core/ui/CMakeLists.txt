# core/ui — everything the screen is made of.
#
#   daw_ui_workspace  the manifests and the layout arithmetic. Pure C++20, no
#                     JUCE: it is the only part of the interface with a right
#                     answer, so it is the part a test can pin down. Always
#                     built, always tested.
#
#   daw_ui            tokens, look and feel, panels, layout host. Needs JUCE.
#                     An INTERFACE library: its sources compile inside the
#                     consuming app target, so JUCE module code is compiled
#                     exactly once (no duplicate symbols).

add_library(daw_ui_workspace STATIC
    src/workspace/WorkspaceManifest.cpp
    src/workspace/LayoutTree.cpp)
add_library(daw::ui_workspace ALIAS daw_ui_workspace)

target_include_directories(daw_ui_workspace PUBLIC include)
target_link_libraries(daw_ui_workspace PUBLIC daw::domain)
daw_set_warnings(daw_ui_workspace)

if(NOT DAW_BUILD_APP)
    return()
endif()

juce_add_binary_data(daw_ui_tokens_data
    HEADER_NAME DawTokensData.h
    NAMESPACE DawTokensData
    SOURCES tokens/tokens.json)

# The typefaces travel with the binary: neither family is installed on a stock
# Windows, and a family JUCE cannot resolve falls back to the system sans
# without telling anyone. Both are SIL Open Font License 1.1; the licences sit
# next to the files.
juce_add_binary_data(daw_ui_fonts_data
    HEADER_NAME DawFontsData.h
    NAMESPACE DawFontsData
    SOURCES
        fonts/Inter-Regular.ttf
        fonts/Inter-Medium.ttf
        fonts/Inter-SemiBold.ttf
        fonts/JetBrainsMono-Regular.ttf
        fonts/JetBrainsMono-Medium.ttf)

# The four manifests travel in the binary like the tokens: a workspace that
# depends on a file found next to the executable is a workspace that disappears
# the day the application is installed somewhere else.
juce_add_binary_data(daw_ui_workspaces_data
    HEADER_NAME DawWorkspacesData.h
    NAMESPACE DawWorkspacesData
    SOURCES
        ../../workspaces/beatmaker.json
        ../../workspaces/decouverte.json
        ../../workspaces/film.json
        ../../workspaces/ugc.json)

add_library(daw_ui INTERFACE)
add_library(daw::ui ALIAS daw_ui)

target_sources(daw_ui INTERFACE
    src/Tokens.cpp
    src/Typography.cpp
    src/DawLookAndFeel.cpp
    src/GalleryView.cpp
    src/Workspaces.cpp
    src/PanelRegistry.cpp
    src/WorkspaceView.cpp
    src/panels/PlaceholderPanel.cpp)

target_include_directories(daw_ui INTERFACE include)

target_link_libraries(daw_ui INTERFACE
    daw_ui_workspace
    daw_ui_tokens_data
    daw_ui_fonts_data
    daw_ui_workspaces_data
    juce::juce_gui_basics)
