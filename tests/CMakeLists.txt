add_executable(platform_tests platform_tests/test_platform.c)
target_link_libraries(platform_tests PRIVATE Eisenfront::Platform unity)
add_test(NAME platform_tests COMMAND platform_tests)

add_executable(core_tests core_tests/test_core.c)
target_link_libraries(core_tests PRIVATE Eisenfront::Core unity)
add_test(NAME core_tests COMMAND core_tests)
