add_executable(client-ncurses
  chatclient.cpp
  ChatWindow.cpp
)
target_compile_features(client-ncurses PRIVATE cxx_std_23)
networking_apply_options(client-ncurses)

find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

# Create FindCurses import targets.
add_library(Curses::Curses INTERFACE IMPORTED)
target_include_directories(Curses::Curses INTERFACE ${CURSES_INCLUDE_DIRS})
target_link_libraries(Curses::Curses INTERFACE ${CURSES_LIBRARIES})

target_link_libraries(client-ncurses
  PRIVATE
    WebSocketNetworking::networking
    Curses::Curses
    Threads::Threads
)

include(GNUInstallDirs)
install(TARGETS client-ncurses
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
