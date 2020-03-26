cmake_minimum_required(VERSION 3.2)    
project(hiccups CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads)

add_executable(hiccups src/hiccups.cpp)
target_compile_options(hiccups PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(hiccups Threads::Threads)

install(TARGETS hiccups DESTINATION bin/)