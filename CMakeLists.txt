cmake_minimum_required(VERSION 4.1)
project(tcp_realization)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_STANDARD 23)

#set(SFML_SKIP_INSTALL ON CACHE BOOL "" FORCE)

add_subdirectory(vendors/libuv)
add_subdirectory(vendors/spdlog)

#add_subdirectory(vendors/sfml)

add_executable(tcp_realization src/main.cpp
        include/core/segment.h
        include/core/tcb.h
        include/core/sw.h
        include/network/UdpDriver.h
        include/network/TimerManager.h
        src/network/TimerManager.cpp
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
#        SFML::Window
#        SFML::System
)
