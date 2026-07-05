add_executable(chatserver
  chatserver.cpp
)
target_compile_features(chatserver PRIVATE cxx_std_23)
networking_apply_options(chatserver)

find_package(Threads REQUIRED)

target_link_libraries(chatserver
  PRIVATE
    WebSocketNetworking::networking
    Threads::Threads
)

include(GNUInstallDirs)
install(TARGETS chatserver
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
