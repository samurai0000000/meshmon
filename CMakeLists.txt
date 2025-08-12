# CMakeLists.txt
#
# Copyright (C) 2025, Charles Chiou

cmake_minimum_required(VERSION 3.13)

project(meshmon VERSION 1.3.0 LANGUAGES C CXX ASM)

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

set(WHOAMI_OUTPUT)
execute_process(
  COMMAND whoami
  OUTPUT_VARIABLE WHOAMI_OUTPUT
  RESULT_VARIABLE WHOAMI_RESULT
  ERROR_VARIABLE WHOAMI_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
set(HOSTNAME_OUTPUT)
execute_process(
  COMMAND hostname
  OUTPUT_VARIABLE HOSTNAME_OUTPUT
  RESULT_VARIABLE HOSTNAME_RESULT
  ERROR_VARIABLE HOSTNAME_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
set(DATE_OUTPUT)
execute_process(
  COMMAND date "+%Y-%m-%d %H:%M:%S"
  OUTPUT_VARIABLE DATE_OUTPUT
  RESULT_VARIABLE DATE_RESULT
  ERROR_VARIABLE DATE_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/version.h
  @ONLY
  )

add_executable(meshmon meshmon.cxx MeshMon.cxx MqttClient.cxx)
target_include_directories(meshmon PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(meshmon PRIVATE ${MOSQUITTO_INCLUDE_DIR})
target_link_libraries(meshmon PRIVATE
  libmeshtastic
  ${MOSQUITTO_LIBRARY}
  ${CONFIG++_LIBRARY})
