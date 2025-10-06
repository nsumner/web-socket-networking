
add_executable(client-ncurses
  chatclient.cpp
  ChatWindow.cpp
)

target_compile_features(client-ncurses PUBLIC cxx_std_17)
set_target_properties(client-ncurses
  PROPERTIES
    LINKER_LANGUAGE CXX
    PREFIX ""
)

find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

target_link_libraries(client-ncurses
  networking
  ${CURSES_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
)

install(TARGETS client-ncurses
  RUNTIME DESTINATION bin
)

