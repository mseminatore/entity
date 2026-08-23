cmake_minimum_required(VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set the project name and version
project(entity VERSION 1.0 LANGUAGES CXX)

# warning flags for our own targets (not applied to the testy submodule)
if (MSVC)
	set(ENTITY_WARNING_FLAGS /W4 /WX)
else()
	set(ENTITY_WARNING_FLAGS -Wall -Wextra -Werror)
endif()

#
# add the executable(s)
#
add_executable(entity main.cpp)
target_compile_features(entity PRIVATE cxx_std_20)
target_compile_options(entity PRIVATE ${ENTITY_WARNING_FLAGS})

# testing
enable_testing()
add_subdirectory(testy)
add_subdirectory(tests)
