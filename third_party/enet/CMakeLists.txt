# Vendored ENet 1.3.18 (MIT, https://github.com/lsalzman/enet), used
# for reliable-UDP networking by the Networking module. Vendored as
# source (unlike the header-only libraries elsewhere in third_party)
# because ENet ships as a small set of .c files, not a single header.
include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckTypeSize)
check_function_exists("fcntl" ENET_HAS_FCNTL)
check_function_exists("poll" ENET_HAS_POLL)
check_function_exists("getaddrinfo" ENET_HAS_GETADDRINFO)
check_function_exists("getnameinfo" ENET_HAS_GETNAMEINFO)
check_function_exists("gethostbyname_r" ENET_HAS_GETHOSTBYNAME_R)
check_function_exists("gethostbyaddr_r" ENET_HAS_GETHOSTBYADDR_R)
check_function_exists("inet_pton" ENET_HAS_INET_PTON)
check_function_exists("inet_ntop" ENET_HAS_INET_NTOP)
check_struct_has_member("struct msghdr" "msg_flags" "sys/types.h;sys/socket.h" ENET_HAS_MSGHDR_FLAGS)
set(CMAKE_EXTRA_INCLUDE_FILES "sys/types.h" "sys/socket.h")
check_type_size("socklen_t" ENET_HAS_SOCKLEN_T BUILTIN_TYPES_ONLY)
unset(CMAKE_EXTRA_INCLUDE_FILES)

add_library(enet STATIC
    callbacks.c
    compress.c
    host.c
    list.c
    packet.c
    peer.c
    protocol.c
    unix.c
    win32.c
)
add_library(Eisenfront::Enet ALIAS enet)

target_include_directories(enet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

foreach(feature HAS_FCNTL HAS_POLL HAS_GETADDRINFO HAS_GETNAMEINFO HAS_GETHOSTBYNAME_R
                 HAS_GETHOSTBYADDR_R HAS_INET_PTON HAS_INET_NTOP HAS_MSGHDR_FLAGS HAS_SOCKLEN_T)
    if(ENET_${feature})
        target_compile_definitions(enet PRIVATE ${feature}=1)
    endif()
endforeach()

if(MSVC)
    target_compile_options(enet PRIVATE /W3)
else()
    target_compile_options(enet PRIVATE -Wno-error)
endif()

if(WIN32)
    target_link_libraries(enet PUBLIC winmm ws2_32)
endif()
