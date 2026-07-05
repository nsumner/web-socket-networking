cmake_minimum_required(VERSION 3.28...4.2)
project(wsn-embed LANGUAGES CXX)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." wsn)

add_executable(embed-consumer ../consumer/main.cpp)
target_link_libraries(embed-consumer PRIVATE WebSocketNetworking::networking)
target_compile_features(embed-consumer PRIVATE cxx_std_23)
