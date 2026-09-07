#
# microbenchmarks for the entity library. Forced to build optimized (-O2 /
# /O2) regardless of the top-level configure, since no CMAKE_BUILD_TYPE is
# set by default and timing numbers from an unoptimized build are
# meaningless. Not registered with ctest -- timing isn't a pass/fail
# signal, and would make CI slow/flaky. Run manually:
#   ./build/benchmarks/entity_bench
#
if (MSVC)
	# /RTC1 (added to CMAKE_CXX_FLAGS_DEBUG by default) is incompatible with
	# /O2 below -- strip it here so a Debug-config build of this target still
	# links, since we always want the benchmark optimized regardless of
	# which config was selected.
	string(REGEX REPLACE "/RTC(su|1|c) ?" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

add_executable(entity_bench entity_bench.cpp)
target_include_directories(entity_bench PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/examples)
target_compile_features(entity_bench PRIVATE cxx_std_23)
target_compile_options(entity_bench PRIVATE ${ENTITY_WARNING_FLAGS})

if (MSVC)
	target_compile_options(entity_bench PRIVATE /O2)
else()
	target_compile_options(entity_bench PRIVATE -O2)
endif()
