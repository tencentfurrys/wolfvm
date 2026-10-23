# wolfvm Android shell — links the clean-room core (../src) as libwolfvm.so.
# main.cpp ships the CLI dispatch (run_cli) with #ifndef WOLFVM_AS_LIBRARY
# around int main; the JNI bridge calls run_cli directly.
cmake_minimum_required(VERSION 3.22)
project(wolfvm_jni CXX)

set(SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(wolfvm SHARED
    ${SRC_ROOT}/common_event.cpp
    ${SRC_ROOT}/filecoder.cpp
    ${SRC_ROOT}/game_dat.cpp
    ${SRC_ROOT}/route.cpp
    ${SRC_ROOT}/command.cpp
    ${SRC_ROOT}/map_tree.cpp
    ${SRC_ROOT}/map_tree_open_status.cpp
    ${SRC_ROOT}/database.cpp
    ${SRC_ROOT}/sys_db_basic.cpp
    ${SRC_ROOT}/tile_set_data.cpp
    ${SRC_ROOT}/map_project.cpp
    ${SRC_ROOT}/vm.cpp
    ${SRC_ROOT}/main.cpp
    wolfvm_jni.cpp)

target_include_directories(wolfvm PRIVATE ${SRC_ROOT})
target_compile_definitions(wolfvm PRIVATE WOLFVM_AS_LIBRARY)
target_compile_options(wolfvm PRIVATE -Wall -Wextra)
target_link_libraries(wolfvm PRIVATE log)
