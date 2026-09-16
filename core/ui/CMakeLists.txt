# daw_ui — display layer. Knows JUCE, never knows domain internals directly.
#
# INTERFACE library: its sources compile inside the consuming app target, so
# JUCE module code is compiled exactly once (no duplicate symbols).

juce_add_binary_data(daw_ui_tokens_data
    HEADER_NAME DawTokensData.h
    NAMESPACE DawTokensData
    SOURCES tokens/tokens.json)

add_library(daw_ui INTERFACE)
add_library(daw::ui ALIAS daw_ui)

target_sources(daw_ui INTERFACE
    src/Tokens.cpp
    src/RootView.cpp)

target_include_directories(daw_ui INTERFACE include)

target_link_libraries(daw_ui INTERFACE
    daw_ui_tokens_data
    juce::juce_gui_basics)
