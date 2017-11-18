cmake_minimum_required(VERSION 3.5)
project(lomc)

add_subdirectory(third_party)

add_executable(demo demo.cpp)
target_link_libraries(demo tinypgm)

