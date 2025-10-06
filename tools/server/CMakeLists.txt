
add_executable(chatserver
  chatserver.cpp
)

target_compile_features(chatserver PUBLIC cxx_std_17)
set_target_properties(chatserver
  PROPERTIES
    LINKER_LANGUAGE CXX
    PREFIX ""
)

find_package(Threads REQUIRED)

target_link_libraries(chatserver
  networking
  ${CMAKE_THREAD_LIBS_INIT}
)

install(TARGETS chatserver
  RUNTIME DESTINATION bin
)

