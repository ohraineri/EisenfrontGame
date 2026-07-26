# Vendored GLAD loader for OpenGL 4.6 core, no extensions, generated
# offline with the `glad` (v0.1.36) Python generator against the Khronos
# gl.xml registry. Regenerate with:
#   pip install glad
#   glad --profile core --api gl=4.6 --generator c --spec gl --extensions "" --out-path <dir>
add_library(glad STATIC glad.c)
add_library(Eisenfront::Glad ALIAS glad)

target_include_directories(glad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(NOT WIN32)
    target_link_libraries(glad PRIVATE ${CMAKE_DL_LIBS})
endif()
