
include(FetchContent)

# Use an installed FTXUI if available. Otherwise download a known tarball.
FetchContent_Declare(ftxui
  URL      https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v6.1.9.tar.gz
  URL_HASH SHA256=45819c1e54914783d4a1ca5633885035d74146778a1f74e1213cdb7b76340e71
  SYSTEM
  EXCLUDE_FROM_ALL
  FIND_PACKAGE_ARGS 6 NAMES ftxui
)

# Third-party code has its own warning policies.
set(_networking_saved_warn "${CMAKE_COMPILE_WARNING_AS_ERROR}")
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
FetchContent_MakeAvailable(ftxui)
set(CMAKE_COMPILE_WARNING_AS_ERROR "${_networking_saved_warn}")


add_executable(client-ftxui
  chatclient.cpp
)

target_compile_features(client-ftxui PRIVATE cxx_std_23)
networking_apply_options(client-ftxui)

target_link_libraries(client-ftxui
  PRIVATE WebSocketNetworking::networking
  PRIVATE ftxui::screen
  PRIVATE ftxui::dom
  PRIVATE ftxui::component
)

include(GNUInstallDirs)
install(TARGETS client-ftxui
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if (NETWORKING_EMSCRIPTEN_BUILD)
  target_compile_options(client-ftxui
    PRIVATE
      -pthread
  )
  target_link_options(client-ftxui
    PRIVATE
      -pthread -sASYNCIFY -sPROXY_TO_PTHREAD
  )

  # Fall back to this directory's own binary dir when the output dir is not set.
  if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(_client_ftxui_web_output_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  else()
    set(_client_ftxui_web_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
  endif()

  foreach(file IN ITEMS "index.html" "run_webassembly.py")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${file}"
                   "${_client_ftxui_web_output_dir}/${file}" COPYONLY)
  endforeach()
endif()
