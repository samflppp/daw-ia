# daw_ui — display layer. Knows JUCE, never knows domain internals directly.
#
# INTERFACE library: its sources compile inside the consuming app target, so
# JUCE module code is compiled exactly once (no duplicate symbols).

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

add_library(daw_ui INTERFACE)
add_library(daw::ui ALIAS daw_ui)

target_sources(daw_ui INTERFACE
    src/Tokens.cpp
    src/Typography.cpp
    src/DawLookAndFeel.cpp
    src/GalleryView.cpp
    src/RootView.cpp)

target_include_directories(daw_ui INTERFACE include)

target_link_libraries(daw_ui INTERFACE
    daw_ui_tokens_data
    daw_ui_fonts_data
    juce::juce_gui_basics)
