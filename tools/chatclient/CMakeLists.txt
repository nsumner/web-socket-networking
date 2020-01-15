
add_executable(chatclient
  chatclient.cpp
  ChatWindow.cpp
)

set_target_properties(chatclient
                      PROPERTIES
                      LINKER_LANGUAGE CXX
                      CXX_STANDARD 17
                      PREFIX ""
)

find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

target_link_libraries(chatclient
  networking
  ${CURSES_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
)

install(TARGETS chatclient
  RUNTIME DESTINATION bin
)

