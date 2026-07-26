add_executable(foundation_smoke foundation_smoke/main.c)
target_link_libraries(foundation_smoke PRIVATE Eisenfront::Foundation)
add_test(NAME foundation_smoke COMMAND foundation_smoke)
