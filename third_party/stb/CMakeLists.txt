# Vendored stb_image.h (public domain, https://github.com/nothings/stb),
# used for image decoding by the Texture module.
add_library(stb_image STATIC stb_image_impl.c)
add_library(Eisenfront::StbImage ALIAS stb_image)

target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT WIN32)
    target_link_libraries(stb_image PRIVATE m)
endif()

# Vendored stb_image_write.h (public domain, https://github.com/nothings/stb),
# used for screenshot capture (Game/src/screenshot.c).
add_library(stb_image_write STATIC stb_image_write_impl.c)
add_library(Eisenfront::StbImageWrite ALIAS stb_image_write)

target_include_directories(stb_image_write PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT WIN32)
    target_link_libraries(stb_image_write PRIVATE m)
endif()
