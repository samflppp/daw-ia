# Dependency-free test runner for S1. Replace with a real framework
# (Catch2 or doctest) when the Command Bus lands.
add_executable(daw_domain_tests
    DomainSmokeTests.cpp)
target_link_libraries(daw_domain_tests PRIVATE daw::domain)
daw_set_warnings(daw_domain_tests)

add_test(NAME domain.smoke COMMAND daw_domain_tests)
