# core/ — native C++ module.
#
#   domain/  Business logic. Pure C++20. Never depends on JUCE, Tracktion or ui/.
#   persistence/ The project on disk: SQLite journal, project folder. Depends on
#            domain/ and SQLite, on nothing else.
#   engine/  Adapter between domain/ and Tracktion Engine. Projects the domain
#            state onto a Tracktion Edit; the domain never knows it exists.
#   ui/      Display only: tokens, look-and-feel, panels, layout host.
#   app/     Entry point. Wires domain, engine and ui together.
#   tests/   Test suite for domain/ (runs without JUCE).

add_subdirectory(domain)
add_subdirectory(persistence)

if(DAW_BUILD_APP)
    add_subdirectory(engine)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()

if(DAW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
