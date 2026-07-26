# Vendored cglm headers (MIT, https://github.com/recp/cglm), used
# header-only (CGLM_INLINE, the library's default) by the Camera module
# and anything else needing vector/matrix math.
add_library(cglm INTERFACE)
add_library(Eisenfront::Cglm ALIAS cglm)

target_include_directories(cglm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
