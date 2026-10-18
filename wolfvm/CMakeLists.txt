cmake_minimum_required(VERSION 3.15)
project(wolfvm CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(wolfvm
  src/main.cpp
  src/filecoder.cpp
  src/game_dat.cpp
  src/route.cpp
  src/command.cpp
  src/common_event.cpp)
if(NOT MSVC)
  target_compile_options(wolfvm PRIVATE -Wall -Wextra)
endif()
