cmake_minimum_required(VERSION 3.4.1)

project(vp_node)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

file(GLOB_RECURSE SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/excepts/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nodes/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/objects/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/vp_utils/*.cpp)

add_library(vp_node STATIC
    ${SRC}
)
target_include_directories(vp_node PUBLIC
    ${MPP_INCLUDES}
    ${RGA_INCLUDES}
    ${RKNN_RT_INCLUDES}
    ${SDL2_INCLUDE_DIRS}
)
target_link_directories(vp_node PUBLIC
    ${SDL2_LIBRARY_DIRS}
)
target_link_libraries(vp_node PUBLIC
    ${COMMON_LIBS}
    ${SDL2_LIBRARIES}
    stdc++fs
    videocodec
    rknn_models
    bytetrack
)
target_compile_options(vp_node PRIVATE
    ${SDL2_CFLAGS_OTHER}
)
