
add_executable(chatserver
  chatserver.cpp
)

set_target_properties(chatserver
                      PROPERTIES
                      LINKER_LANGUAGE CXX
                      CXX_STANDARD 17
                      PREFIX ""
)

find_package(Boost 1.68 COMPONENTS system REQUIRED)
find_package(Threads REQUIRED)

target_include_directories(chatserver
  PRIVATE
  ${Boost_INCLUDE_DIR}
  $<TARGET_PROPERTY:networking,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(chatserver
  networking
  ${Boost_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
)

install(TARGETS chatserver
  RUNTIME DESTINATION bin
)

