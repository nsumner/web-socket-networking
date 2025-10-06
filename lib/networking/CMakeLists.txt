
add_library(networking
  src/Server.cpp
  src/Client.cpp
)

target_include_directories(networking
  PUBLIC
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (NETWORKING_EMSCRIPTEN_BUILD)
  target_link_libraries(networking
    PRIVATE
      websocket.js
  )
else()
  find_package(Boost 1.72 CONFIG COMPONENTS)
  target_link_libraries(networking
    PRIVATE
      Boost::boost
  )
endif()

target_compile_features(networking PUBLIC cxx_std_17)
set_target_properties(networking
  PROPERTIES
    LINKER_LANGUAGE CXX
)

install(TARGETS networking
  ARCHIVE DESTINATION lib
)

