cmake_minimum_required(VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set the project name and version
project(entity VERSION 1.0 LANGUAGES CXX)

# testing
#enable_testing()
#set(test_file ${CMAKE_CURRENT_SOURCE_DIR}/test.asm)
#add_test(NAME assemble COMMAND as09 -x ${test_file})

#
# add the executable(s)
#
add_executable(entity main.cpp)
target_compile_features(entity PRIVATE cxx_std_20)
