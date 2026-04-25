cmake_minimum_required(VERSION 3.20)
project(MyuTCP)
set(CMAKE_CXX_STANDARD 23)

if(NOT TARGET uv_a)
    add_subdirectory(vendors/libuv)
endif()
if(NOT TARGET spdlog)
    add_subdirectory(vendors/spdlog)
endif()

option(MYU_ENABLE_DEBUG "Enable MyuTCP verbose debug logging" OFF)


add_library(myutcp STATIC
        src/core/tcb.cpp
        src/network/TimerManager.cpp
        src/network/UdpDriver.cpp
        include/util/logger.h
)

target_include_directories(myutcp PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/core>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/network>
)

target_link_libraries(myutcp PUBLIC
        uv_a
        spdlog
)

if(MYU_ENABLE_DEBUG)
    message(STATUS ">> MyuTCP: Debug logging ENABLED")
    target_compile_definitions(myutcp PUBLIC MYU_ENABLE_DEBUG)
else()
    message(STATUS ">> MyuTCP: Debug logging DISABLED")
endif()

add_executable(tcp_realization
        include/core/segment.h
        include/core/tcb.h
        include/core/sw.h
        include/network/UdpDriver.h
        include/network/TimerManager.h
        include/core/RingBuffer.h
        src/core/tcb.cpp
        include/core/stack.h
        include/util/gen_iss.h
        include/util/parse_ip.h
        test/server_instance.cpp
)

target_link_libraries(tcp_realization PRIVATE myutcp)


if(MINGW)
    target_link_options(tcp_realization PRIVATE -static-libgcc -static-libstdc++ -Wl,--allow-multiple-definition)
endif()

