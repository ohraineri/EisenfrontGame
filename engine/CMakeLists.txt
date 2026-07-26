# --- Foundation ---------------------------------------------------------
add_library(eisenfront_foundation STATIC
    src/foundation/ef_assert.c
    src/foundation/ef_error.c
    src/foundation/ef_log.c
    src/foundation/ef_timer.c
    src/foundation/ef_config.c
    src/foundation/ef_module.c
    src/foundation/ef_engine.c
)
add_library(Eisenfront::Foundation ALIAS eisenfront_foundation)

target_include_directories(eisenfront_foundation
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_foundation PUBLIC c_std_23)

target_compile_options(eisenfront_foundation PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

# SDL3 is a private implementation detail of Foundation's timer and log
# subsystems (see ef_timer.c, ef_log.c) and is never included from a public
# Foundation header, so it is not propagated to Foundation's consumers.
target_link_libraries(eisenfront_foundation PRIVATE SDL3::SDL3)
