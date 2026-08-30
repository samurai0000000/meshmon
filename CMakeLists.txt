# CMakeLists.txt
#
# Copyright (C) 2025, Charles Chiou

cmake_minimum_required(VERSION 3.13)

project(meshmon VERSION 2.0.1 LANGUAGES C CXX ASM)

find_package(Mosquitto QUIET)
if (NOT Mosquitto_FOUND)
  find_path(MOSQUITTO_INCLUDE_DIR NAMES mosquitto.h)
  find_library(MOSQUITTO_LIBRARY NAMES mosquitto libmosquitto)
endif ()
if (NOT MOSQUITTO_INCLUDE_DIR OR NOT MOSQUITTO_LIBRARY)
  message(FATAL_ERROR "libmosquitto not found")
endif ()

find_path(CONFIG++_INCLUDE_DIR NAMES libconfig.h++)
find_library(CONFIG++_LIBRARY NAMES config++ libconfig++)
if (NOT CONFIG++_INCLUDE_DIR OR NOT CONFIG++_LIBRARY)
  message(FATAL_ERROR "libconfig++ not found")
endif ()

find_package(CURL QUIET)
if (NOT CURL_FOUND)
  find_path(CURL_INCLUDE_DIR NAMES curl/curl.h
    PATHS
      /usr/include
      /usr/include/${CMAKE_LIBRARY_ARCHITECTURE}
      /usr/local/include
      /usr/local/include/${CMAKE_LIBRARY_ARCHITECTURE})
  find_library(CURL_LIBRARY NAMES curl libcurl)
endif ()
if (CURL_FOUND)
  if (NOT CURL_INCLUDE_DIR AND CURL_INCLUDE_DIRS)
    set(CURL_INCLUDE_DIR ${CURL_INCLUDE_DIRS})
  endif ()
  if (NOT CURL_LIBRARY AND CURL_LIBRARIES)
    set(CURL_LIBRARY ${CURL_LIBRARIES})
  endif ()
endif ()
if (NOT CURL_INCLUDE_DIR OR NOT CURL_LIBRARY)
  if (NOT TARGET CURL::libcurl)
    message(FATAL_ERROR "libcurl not found")
  endif ()
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

add_executable(meshmon meshmon.cxx MeshMon.cxx MeshMonShell.cxx MqttClient.cxx
  ChatBot.cxx GeminiChat.cxx Calibration.cxx)
target_include_directories(meshmon PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(meshmon PRIVATE
  ${MOSQUITTO_INCLUDE_DIR}
  ${CONFIG++_INCLUDE_DIR}
  ${CURL_INCLUDE_DIR})
target_link_libraries(meshmon PRIVATE
  libmeshtastic
  ${MOSQUITTO_LIBRARY}
  ${CONFIG++_LIBRARY})
if (TARGET CURL::libcurl)
  target_link_libraries(meshmon PRIVATE CURL::libcurl)
else ()
  target_link_libraries(meshmon PRIVATE ${CURL_LIBRARY})
endif ()
