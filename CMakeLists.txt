cmake_minimum_required(VERSION 3.10)

# set the project name and version
project(entity VERSION 1.0 LANGUAGES CXX)
#project(entity VERSION 1.0 LANGUAGES C++)

# testing
#enable_testing()
#set(test_file ${CMAKE_CURRENT_SOURCE_DIR}/test.asm)
#add_test(NAME assemble COMMAND as09 -x ${test_file})

#
# add the executable(s)
#
add_executable(entity main.cpp)