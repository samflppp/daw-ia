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

# ui/ is always added: the workspace manifests and the layout arithmetic inside
# it know nothing of JUCE, and they are tested in the fast preset. The targets
# that do need JUCE are guarded inside that file.
add_subdirectory(ui)

if(DAW_BUILD_APP)
    add_subdirectory(engine)
    add_subdirectory(app)
endif()

if(DAW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
