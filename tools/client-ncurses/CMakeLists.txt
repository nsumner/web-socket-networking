
add_executable(client-ncurses
  chatclient.cpp
  ChatWindow.cpp
)

set_target_properties(client-ncurses
                      PROPERTIES
                      LINKER_LANGUAGE CXX
                      CXX_STANDARD 17
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

