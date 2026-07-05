add_library(networking)
add_library(WebSocketNetworking::networking ALIAS networking)

target_sources(networking
  PRIVATE
    src/Server.cpp
    src/Client.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS include
      FILES
        include/Client.h
        include/Server.h
)

target_include_directories(networking
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(networking PUBLIC cxx_std_23)
set_target_properties(networking PROPERTIES CXX_EXTENSIONS OFF)
networking_apply_options(networking)

if(NETWORKING_EMSCRIPTEN_BUILD)
  target_link_libraries(networking
    PRIVATE
      websocket.js
  )
else()
  find_package(Boost 1.83 REQUIRED CONFIG)
  target_link_libraries(networking
    PRIVATE
      Boost::headers
  )
endif()

if(NETWORKING_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS networking
    EXPORT WebSocketNetworkingTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(EXPORT WebSocketNetworkingTargets
    NAMESPACE WebSocketNetworking::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WebSocketNetworking
  )

  configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/WebSocketNetworkingConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WebSocketNetworkingConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WebSocketNetworking
  )
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/WebSocketNetworkingConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/WebSocketNetworkingConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/WebSocketNetworkingConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WebSocketNetworking
  )
endif()
