# Vendored miniaudio.h (public domain / MIT-0, https://github.com/mackron/miniaudio),
# used for audio device I/O, mixing, and decoding by the Audio module.
add_library(miniaudio STATIC miniaudio_impl.c)
add_library(Eisenfront::Miniaudio ALIAS miniaudio)

target_include_directories(miniaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT WIN32)
    target_link_libraries(miniaudio PRIVATE m dl pthread)
endif()
