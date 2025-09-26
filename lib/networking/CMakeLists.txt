
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
  add_boost(
    TRY_BOOST_VERSION BOOST_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED
    BOOST_NOT_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED networking
  )

endif()

set_target_properties(networking
                      PROPERTIES
                      LINKER_LANGUAGE CXX
                      CXX_STANDARD 23
)

install(TARGETS networking
  ARCHIVE DESTINATION lib
)

