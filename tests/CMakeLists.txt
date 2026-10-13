add_executable(vw_tests main.cpp core_tests.cpp)
target_link_libraries(vw_tests PRIVATE vw_core vw_net doctest::doctest)
vw_target_defaults(vw_tests)
add_test(NAME vw_tests COMMAND vw_tests)
