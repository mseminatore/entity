#
# unit tests for the entity library, using the testy test framework
#
add_executable(entity_tests entity_tests.cpp)
target_include_directories(entity_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/testy)
target_link_libraries(entity_tests PRIVATE testy)
target_compile_features(entity_tests PRIVATE cxx_std_23)
target_compile_options(entity_tests PRIVATE ${ENTITY_WARNING_FLAGS})

add_test(NAME entity_tests COMMAND entity_tests)

#
# stress tests: larger-scale / randomized scenarios, kept in a separate
# target so the fast unit test suite above stays quick. Run the full
# suite with `ctest`, or skip these with `ctest -LE stress`.
#
add_executable(entity_stress_tests entity_stress_tests.cpp)
target_include_directories(entity_stress_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/testy)
target_link_libraries(entity_stress_tests PRIVATE testy)
target_compile_features(entity_stress_tests PRIVATE cxx_std_23)
target_compile_options(entity_stress_tests PRIVATE ${ENTITY_WARNING_FLAGS})

add_test(NAME entity_stress_tests COMMAND entity_stress_tests)
set_tests_properties(entity_stress_tests PROPERTIES LABELS "stress")
