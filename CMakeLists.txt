cmake_minimum_required(VERSION 3.30)
project(WebSocketNetworking
  VERSION 0.3
  LANGUAGES CXX
)

set(PACKAGE_NAME WebSocketNetworking)
set(PACKAGE_VERSION 0.3)
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "wsumner@sfu.ca")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fno-rtti")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

set(AddBoost.cmake_VERSION 3.7.3)
CPMAddPackage(
  NAME AddBoost.cmake
  VERSION "${AddBoost.cmake_VERSION}"
  URL "https://github.com/Arniiiii/AddBoost.cmake/archive/refs/tags/${AddBoost.cmake_VERSION}.tar.gz"
)

set(TRY_BOOST_VERSION "1.88.0")
set(BOOST_MY_OPTIONS "BOOST_ENABLE_PYTHON ON;")
set(BOOST_NOT_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED "system")
set(BOOST_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED "asio;beast")

if (POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL Emscripten)
  set(NETWORKING_EMSCRIPTEN_BUILD ON)
  set(CMAKE_CXX_FLAGS_DEBUG
      "-O0 -g -fno-omit-frame-pointer"
  )
else()
  set(NETWORKING_EMSCRIPTEN_BUILD OFF)
  set(CMAKE_CXX_FLAGS_DEBUG
      "-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
  )
endif()

option(ENABLE_FLUTTER_CLIENT "Build the Flutter client" ON)
option(ENABLE_FTXUI_CLIENT   "Build the FTXUI client" ON)
option(ENABLE_NCURSES_CLIENT "Build the NCurses client" ON)

add_subdirectory(lib)
add_subdirectory(tools)


