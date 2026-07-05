cmake_minimum_required(VERSION 3.28...4.2)
project(WebSocketNetworking
  VERSION 0.4
  DESCRIPTION "Simple single threaded websocket networking"
  LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(NETWORKING_EMSCRIPTEN_BUILD ON)
else()
  set(NETWORKING_EMSCRIPTEN_BUILD OFF)
endif()

option(NETWORKING_BUILD_TOOLS    "Build the demo server and clients" ${PROJECT_IS_TOP_LEVEL})
option(NETWORKING_CLIENT_FLUTTER "Build the Flutter client" OFF)
option(NETWORKING_CLIENT_FTXUI   "Build the FTXUI client" OFF)
option(NETWORKING_CLIENT_NCURSES "Build the NCurses client" ON)
option(NETWORKING_BUILD_TESTS    "Build the networking library tests" OFF)
option(NETWORKING_INSTALL        "Configure networking library installation" ${PROJECT_IS_TOP_LEVEL})

option(NETWORKING_ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan" OFF)
option(NETWORKING_NO_RTTI           "Build without C++ RTTI" OFF)

if(NETWORKING_ENABLE_SANITIZERS AND NETWORKING_EMSCRIPTEN_BUILD)
  message(FATAL_ERROR "NETWORKING_ENABLE_SANITIZERS is not supported for Emscripten")
endif()
if(NETWORKING_ENABLE_SANITIZERS
    AND NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
  message(FATAL_ERROR
    "NETWORKING_ENABLE_SANITIZERS requires GCC or Clang. "
    "${CMAKE_CXX_COMPILER_ID} is not supported.")
endif()

include(cmake/NetworkingCompileOptions.cmake)

add_subdirectory(lib)

if(NETWORKING_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(NETWORKING_BUILD_TESTS AND NOT NETWORKING_EMSCRIPTEN_BUILD)
  enable_testing()
  add_subdirectory(test)
endif()
