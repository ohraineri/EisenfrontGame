add_executable(platform_tests platform_tests/test_platform.c)
target_link_libraries(platform_tests PRIVATE Eisenfront::Platform unity)
add_test(NAME platform_tests COMMAND platform_tests)

add_executable(core_tests core_tests/test_core.c)
target_link_libraries(core_tests PRIVATE Eisenfront::Core unity)
add_test(NAME core_tests COMMAND core_tests)

add_executable(window_tests window_tests/test_window.c)
target_link_libraries(window_tests PRIVATE Eisenfront::Window unity)
add_test(NAME window_tests COMMAND window_tests)
# SDL3's offscreen video driver needs no real display, X server, or GPU
# (exactly what headless CI and this sandbox have) and, unlike "dummy",
# still honors SDL_WINDOW_OPENGL, which Window always requests.
set_tests_properties(window_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(input_tests input_tests/test_input.c)
target_link_libraries(input_tests PRIVATE Eisenfront::Input SDL3::SDL3 unity)
add_test(NAME input_tests COMMAND input_tests)
set_tests_properties(input_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")
