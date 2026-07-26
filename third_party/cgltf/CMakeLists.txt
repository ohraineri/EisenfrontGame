# Vendored cgltf.h (MIT, https://github.com/jkuhlmann/cgltf), used for
# glTF model parsing by the Asset Manager's model loader.
add_library(cgltf STATIC cgltf_impl.c)
add_library(Eisenfront::Cgltf ALIAS cgltf)

target_include_directories(cgltf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT WIN32)
    target_link_libraries(cgltf PRIVATE m)
endif()
