add_executable(platform_tests platform_tests/test_platform.c)
target_link_libraries(platform_tests PRIVATE Eisenfront::Platform unity)
add_test(NAME platform_tests COMMAND platform_tests)
