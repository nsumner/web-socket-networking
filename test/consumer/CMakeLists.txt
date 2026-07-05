cmake_minimum_required(VERSION 3.28...4.2)
project(wsn-consumer LANGUAGES CXX)

find_package(WebSocketNetworking 0.4 REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE WebSocketNetworking::networking)
target_compile_features(consumer PRIVATE cxx_std_23)
