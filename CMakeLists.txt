cmake_minimum_required(VERSION 4.1)
project(tcp_realization)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_STANDARD 23)

set(SFML_SKIP_INSTALL ON CACHE BOOL "" FORCE)

add_subdirectory(vendors/libuv)
add_subdirectory(vendors/spdlog)
#add_subdirectory(vendors/raylib)
#add_subdirectory(vendors/sfml)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Clang detected, configuring AddressSanitizer...")
#    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
#
#    add_compile_options(-ftrivial-auto-var-init=pattern)
endif()


if(MINGW)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,--allow-multiple-definition")
endif()

add_executable(tcp_realization
        include/core/segment.h
        include/core/tcb.h
        include/core/sw.h
        include/network/UdpDriver.h
        include/network/TimerManager.h
        src/network/TimerManager.cpp
        src/network/UdpDriver.cpp
        include/core/RingBuffer.h
        src/core/tcb.cpp
        include/core/stack.h
        include/util/gen_iss.h
        include/util/parse_ip.h
#        test/client_instance.cpp
        test/server_instance.cpp
#        src/main.cpp
)

target_include_directories(tcp_realization PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/core
        ${CMAKE_CURRENT_SOURCE_DIR}/include/network
)

target_link_libraries(tcp_realization PRIVATE
        uv_a
        spdlog
#        SFML::Graphics
#        raylib
)

