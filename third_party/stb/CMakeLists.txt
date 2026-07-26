# Vendored stb_image.h (public domain, https://github.com/nothings/stb),
# used for image decoding by the Texture module.
add_library(stb_image STATIC stb_image_impl.c)
add_library(Eisenfront::StbImage ALIAS stb_image)

target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT WIN32)
    target_link_libraries(stb_image PRIVATE m)
endif()
