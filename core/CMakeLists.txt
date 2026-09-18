# core/ — native C++ module.
#
#   domain/  Business logic. Pure C++20. Never depends on JUCE, Tracktion or ui/.
#   engine/  Adapter between domain/ and Tracktion Engine. Projects the domain
#            state onto a Tracktion Edit; the domain never knows it exists.
#   ui/      Display only: tokens, look-and-feel, panels, layout host.
#   app/     Entry point. Wires domain, engine and ui together.
#   tests/   Test suite for domain/ (runs without JUCE).

add_subdirectory(domain)

if(DAW_BUILD_APP)
    add_subdirectory(engine)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()

if(DAW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
