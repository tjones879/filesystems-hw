cmake_minimum_required(VERSION 3.2)
project(FS)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Boost REQUIRED COMPONENTS iostreams)

SET( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -I/usr/include/fuse3" )
SET( CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -lfuse3" )

include_directories(${Boost_INCLUDE_DIRS})

add_executable(FS main.cpp)
target_link_libraries(FS ${Boost_LIBRARIES})
