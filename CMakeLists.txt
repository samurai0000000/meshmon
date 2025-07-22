# CMakeLists.txt
#
# Copyright (C) 2025, Charles Chiou

cmake_minimum_required(VERSION 3.0)

project(meshmon)

find_package(Protobuf REQUIRED)

add_subdirectory(libmeshtastic)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -frtti")

include_directories(${CMAKE_CURRENT_LIST_DIR})
add_compile_options(-Wall -Wextra -Werror)
add_compile_options(-g -O2)

add_executable(meshmon meshmon.cxx MeshMon.cxx)
target_link_libraries(meshmon PRIVATE libmeshtastic protobuf-c)
