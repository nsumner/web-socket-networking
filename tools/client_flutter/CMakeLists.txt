find_program(FLUTTER_CMD flutter)

if(NOT FLUTTER_CMD)
  message(STATUS "Flutter not found. Skipping flutter chat client build")
  return()
endif()

message(STATUS "Flutter located at ${FLUTTER_CMD}. Proceeding with flutter client.")

# Stage into a subdirectory of our own binary dir.
set(FLUTTER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}")
set(FLUTTER_BUILD  "${CMAKE_CURRENT_BINARY_DIR}/flutter_app")

# Any dart source change must retrigger the sync, so glob the real list.
file(GLOB_RECURSE FLUTTER_SOURCES CONFIGURE_DEPENDS
  "${FLUTTER_SOURCE}/lib/*.dart"
  "${FLUTTER_SOURCE}/integration_test/*.dart")

add_custom_command(
  OUTPUT "${FLUTTER_BUILD}/.sources.stamp"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          "${FLUTTER_SOURCE}/lib" "${FLUTTER_BUILD}/lib"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          "${FLUTTER_SOURCE}/integration_test" "${FLUTTER_BUILD}/integration_test"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${FLUTTER_SOURCE}/pubspec.yaml" "${FLUTTER_BUILD}/pubspec.yaml"
  COMMAND ${CMAKE_COMMAND} -E touch "${FLUTTER_BUILD}/.sources.stamp"
  DEPENDS ${FLUTTER_SOURCES} "${FLUTTER_SOURCE}/pubspec.yaml"
  COMMENT "Syncing flutter client sources into ${FLUTTER_BUILD}"
  VERBATIM
)

add_custom_command(
  OUTPUT "${FLUTTER_BUILD}/.initialized.stamp"
  COMMAND ${FLUTTER_CMD} create "${FLUTTER_BUILD}"
  COMMAND ${CMAKE_COMMAND} -E touch "${FLUTTER_BUILD}/.initialized.stamp"
  DEPENDS "${FLUTTER_BUILD}/.sources.stamp"
  COMMENT "Reinitializing flutter project in ${FLUTTER_BUILD}"
  VERBATIM
)

add_custom_target(flutter_client ALL
  DEPENDS "${FLUTTER_BUILD}/.initialized.stamp"
)
