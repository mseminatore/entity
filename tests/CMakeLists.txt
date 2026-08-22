#
# unit tests for the entity library, using the testy test framework
#
add_executable(entity_tests entity_tests.cpp)
target_include_directories(entity_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/testy)
target_link_libraries(entity_tests PRIVATE testy)
target_compile_features(entity_tests PRIVATE cxx_std_20)

add_test(NAME entity_tests COMMAND entity_tests)
