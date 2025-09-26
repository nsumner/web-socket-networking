find_package(Curses)
find_package(Threads)

function(build_ncurses_client)

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

  target_link_libraries(client-ncurses
    networking
    ${CURSES_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
  )

  install(TARGETS client-ncurses
    RUNTIME DESTINATION bin
  )

endfunction(build_ncurses_client)

if (NOT Curses)
  message("Curses not found. Skipping ncurses chat client build.")
elseif(NOT Threads)
  message("Threads not found. Skipping ncurses chat client build.")
else()
  message("Curses located at ${Curses} and Threads located at ${Threads}. Proceeding with ncurses client.")
  build_ncurses_client()
endif()