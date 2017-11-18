cmake_minimum_required(VERSION 3.5)
project(lomc)

add_subdirectory(third_party)

set(demo_sources
    demo.cpp
    image.hpp
    )

add_executable(demo ${demo_sources})
target_link_libraries(demo tinypgm)

