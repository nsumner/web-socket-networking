
find_program(FLUTTER_CMD flutter)

set(FLUTTER_BUILD "${CMAKE_BINARY_DIR}/client_flutter")
set(FLUTTER_SOURCE "${CMAKE_SOURCE_DIR}/tools/client_flutter")

# By copying the files into the build directory on demand (when modified),
# we can still get an out of source build while working with a Flutter
# subproject.
function(copy_sources_when_fresh)
  add_custom_command(
    OUTPUT
      ${FLUTTER_BUILD}/pubspec.yaml
      ${FLUTTER_BUILD}/lib/main.dart
      ${FLUTTER_BUILD}/integration_test/main_test.dart
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${FLUTTER_SOURCE}/lib
            ${FLUTTER_BUILD}/lib
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${FLUTTER_SOURCE}/integration_test
            ${FLUTTER_BUILD}/integration_test
    COMMAND ${CMAKE_COMMAND} -E copy
            ${FLUTTER_SOURCE}/pubspec.yaml
            ${FLUTTER_BUILD}/pubspec.yaml
    DEPENDS
      ${FLUTTER_SOURCE}/pubspec.yaml
      ${FLUTTER_SOURCE}/lib/main.dart
      ${FLUTTER_SOURCE}/integration_test/main_test.dart
    COMMENT "Copying flutter client to ${FLUTTER_BUILD}"
    VERBATIM
  )
  add_custom_target(flutter_client_source
    DEPENDS
      ${FLUTTER_BUILD}/pubspec.yaml
      ${FLUTTER_BUILD}/lib/main.dart
      ${FLUTTER_BUILD}/integration_test/main_test.dart
  )
  set_property(TARGET flutter_client_source PROPERTY EXCLUDE_FROM_ALL OFF)
endfunction(copy_sources_when_fresh)


function(build_flutter_client)
  add_custom_command(
    OUTPUT
      ${FLUTTER_BUILD}/build
    COMMAND ${FLUTTER_CMD} create ${FLUTTER_BUILD}
    DEPENDS
      ${FLUTTER_BUILD}/pubspec.yaml
      ${FLUTTER_BUILD}/lib/main.dart
      ${FLUTTER_BUILD}/integration_test/main_test.dart
    COMMENT "Reinitializing flutter project in ${FLUTTER_BUILD}"
    VERBATIM
  )
  add_custom_target(flutter_client_initialization
    DEPENDS ${FLUTTER_BUILD}/build
  )
  set_property(TARGET flutter_client_initialization PROPERTY EXCLUDE_FROM_ALL OFF)

  # We could add extra infrastructure here for building the demo
  # for a specific target, but the build directory of the project
  # makes that easy enough using basic flutter commands. Maybe I'll
  # add that later.
endfunction(build_flutter_client)


if (FLUTTER_CMD)
  message("Flutter located at ${FLUTTER_CMD}. Proceeding with flutter client.")
  copy_sources_when_fresh()
  build_flutter_client()  
  
else()
  message("Flutter not found. Skipping flutter chat client build")
endif()

