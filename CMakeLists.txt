# CMakeLists.txt
#
# Copyright (C) 2025, Charles Chiou

cmake_minimum_required(VERSION 3.13)

project(meshmon)

find_package(Mosquitto QUIET)
if (NOT Mosquitto_FOUND)
  find_path(MOSQUITTO_INCLUDE_DIR NAMES mosquitto.h)
  find_library(MOSQUITTO_LIBRARY NAMES mosquitto libmosquitto)
endif ()

add_subdirectory(libmeshtastic)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -frtti")

include_directories(${CMAKE_CURRENT_LIST_DIR})
add_compile_options(-Wall -Wextra -Werror)
add_compile_options(-g -O2)

add_executable(meshmon meshmon.cxx MeshMon.cxx MqttClient.cxx)
target_include_directories(meshmon PRIVATE ${MOSQUITTO_INCLUDE_DIR})
target_link_libraries(meshmon PRIVATE
  libmeshtastic
  ${MOSQUITTO_LIBRARY}
  ${CONFIG++_LIBRARY})
