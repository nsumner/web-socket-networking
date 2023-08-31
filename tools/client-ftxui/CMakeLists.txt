
include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG v5.0.0
)

FetchContent_GetProperties(ftxui)
if(NOT ftxui_POPULATED)
  FetchContent_Populate(ftxui)
  add_subdirectory(${ftxui_SOURCE_DIR} ${ftxui_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()


add_executable(client-ftxui
  chatclient.cpp
)

set_target_properties(client-ftxui PROPERTIES
  LINKER_LANGUAGE CXX
  CXX_STANDARD 17
  PREFIX ""
)

target_link_libraries(client-ftxui
  PRIVATE networking
  PRIVATE ftxui::screen
  PRIVATE ftxui::dom
  PRIVATE ftxui::component
)

install(TARGETS client-ftxui
  RUNTIME DESTINATION bin
)

if (NETWORKING_EMSCRIPTEN_BUILD)
  target_compile_options(client-ftxui
    PRIVATE
      -s USE_PTHREADS
  )
  # NOTE: emscripten intercepts linker arguments, so passing custom
  #  arguments means that we need to remove the compiler specific flags
  #  that CMake would otherwise add in.
  set (CMAKE_CXX_LINKER_WRAPPER_FLAG " ")
  target_link_options(client-ftxui
    PRIVATE
      "LINKER:-sUSE_PTHREADS,-sASYNCIFY,-sPROXY_TO_PTHREAD"
  )

  foreach(file "index.html" "run_webassembly.py")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${file}" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${file})
  endforeach(file)
endif()
