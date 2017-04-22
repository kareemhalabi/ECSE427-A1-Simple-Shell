cmake_minimum_required(VERSION 3.6)
project(Simple_Shell)

set(CMAKE_C_STANDARD 99)

set(SOURCE_FILES simple_shell.c)
add_executable(Simple_Shell ${SOURCE_FILES})