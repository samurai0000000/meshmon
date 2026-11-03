# CMakeLists.txt
#
# Copyright (C) 2025, Charles Chiou

cmake_minimum_required(VERSION 3.13)

project(meshmon VERSION 2.1.21 LANGUAGES C CXX ASM)

find_package(SQLite3 REQUIRED)
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
      /usr/local/include/${CMAKE_LIBRARY_ARCHITECTURE}
      $ENV{HOME}/work/rabbit/build/aarch64/librealsense/third-party/libcurl/include)
  find_library(CURL_LIBRARY NAMES curl libcurl libcurl.so.4
    PATHS
      /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
      /usr/lib
      /usr/local/lib)
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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -frtti")

include_directories(${CMAKE_CURRENT_LIST_DIR})
add_compile_options(-Wall -Wextra -Werror)
add_compile_options(-g -O2)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-error -O0)
  foreach (t mynodeid nodelisten nodereboot)
    if (TARGET ${t})
      target_compile_options(${t} PRIVATE -Wno-error -O0)
    endif ()
  endforeach ()
endif ()

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

find_package(Threads REQUIRED)
find_package(Curses REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CPPUTEST REQUIRED cpputest)

add_library(meshmon_core STATIC
  MeshMon.cxx MeshMonShell.cxx MqttClient.cxx
  ChatBot.cxx GeminiChat.cxx Calibration.cxx MeshMonDb.cxx
  AutomationDevice.cxx MeshPumpDevice.cxx MeshRoofDevice.cxx MeshRoomDevice.cxx
  AimonGatewayClient.cxx WebServer.cxx SpatialAnalytics.cxx InstanceLock.cxx
  NcursesConsole.cxx)
target_include_directories(meshmon_core PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/libmeshtastic
  ${CMAKE_CURRENT_SOURCE_DIR}/libmeshtastic/Meshtastic-arduino/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib
  ${CURSES_INCLUDE_DIRS}
  ${SQLite3_INCLUDE_DIRS}
  ${MOSQUITTO_INCLUDE_DIR}
  ${CONFIG++_INCLUDE_DIR}
  ${CURL_INCLUDE_DIR})
target_link_libraries(meshmon_core PRIVATE
  libmeshtastic)
target_link_libraries(meshmon_core PUBLIC
  Threads::Threads
  ${CURSES_LIBRARIES}
  ${SQLite3_LIBRARIES}
  ${MOSQUITTO_LIBRARY}
  ${CONFIG++_LIBRARY})
if (TARGET CURL::libcurl)
  target_link_libraries(meshmon_core PUBLIC CURL::libcurl)
else ()
  target_link_libraries(meshmon_core PUBLIC ${CURL_LIBRARY})
endif ()

add_executable(meshmon meshmon.cxx)
target_link_libraries(meshmon PRIVATE meshmon_core)

add_executable(test_meshmon_suite
  test/TestMain.cxx
  test/TestProtocolAndCalib.cxx
  test/TestConcurrencyAndResilience.cxx
  test/TestMeshMonDbAndAnalytics.cxx
  test/TestWebServerAuth.cxx)
target_compile_definitions(test_meshmon_suite PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED=1)
target_include_directories(test_meshmon_suite PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/test)
target_include_directories(test_meshmon_suite SYSTEM PRIVATE
  ${CPPUTEST_INCLUDE_DIRS})
target_link_libraries(test_meshmon_suite PRIVATE
  meshmon_core
  ${CPPUTEST_LIBRARIES})
if (CPPUTEST_LIBRARY_DIRS)
  target_link_directories(test_meshmon_suite PRIVATE ${CPPUTEST_LIBRARY_DIRS})
endif ()

add_executable(test_meshmon_fuzz
  test/TestMeshMonFuzz.cxx)
target_include_directories(test_meshmon_fuzz PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/test)
target_link_libraries(test_meshmon_fuzz PRIVATE
  meshmon_core
  rapidcheck)

add_custom_target(test
  COMMAND test_meshmon_suite -v
  COMMAND test_meshmon_fuzz
  DEPENDS test_meshmon_suite test_meshmon_fuzz)
