cmake_minimum_required(VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set the project name and version
project(entity VERSION 1.0 LANGUAGES CXX)

#
# add the executable(s)
#
add_executable(entity main.cpp)
target_compile_features(entity PRIVATE cxx_std_20)

# testing
enable_testing()
add_subdirectory(testy)
add_subdirectory(tests)
