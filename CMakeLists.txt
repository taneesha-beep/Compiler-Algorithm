cmake_minimum_required(VERSION 3.20)

project(algo)

set(CMAKE_CXX_STANDARD 20)

# The front end and engines, built once and shared. `main.cpp` stays out of it
# so that a test binary can link the same objects alongside its own main().
add_library(algo_core STATIC
    src/diagnostic.cpp
    src/lexer.cpp
    src/parser.cpp
    src/resolver.cpp
    src/interpreter.cpp)
target_include_directories(algo_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(algo src/main.cpp)
target_link_libraries(algo PRIVATE algo_core)

enable_testing()

# Unit tests. A test is a plain C++ binary linking algo_core that exits
# non-zero when a check fails, which is all CTest needs to report a failure —
# no third-party test framework, in keeping with the project having no
# external dependencies. Each new unit test gets its own target listed here;
# these are compiled sources, so they are not globbed.
add_executable(span_test tests/span_test.cpp)
target_link_libraries(span_test PRIVATE algo_core)
add_test(NAME span_test COMMAND span_test)

add_executable(diagnostic_test tests/diagnostic_test.cpp)
target_link_libraries(diagnostic_test PRIVATE algo_core)
add_test(NAME diagnostic_test COMMAND diagnostic_test)

add_executable(expression_test tests/expression_test.cpp)
target_link_libraries(expression_test PRIVATE algo_core)
add_test(NAME expression_test COMMAND expression_test)

add_executable(resolver_test tests/resolver_test.cpp)
target_link_libraries(resolver_test PRIVATE algo_core)
add_test(NAME resolver_test COMMAND resolver_test)

# Golden-file cases: run tests/<name>.algo and compare stdout against
# tests/<name>.expected, plus stderr and the exit code where the case supplies
# a .expected_err or .expected_code. run_case.cmake runs the interpreter from
# tests/ with a bare filename, so a diagnostic's path is machine-independent.
file(GLOB ALGO_TEST_CASES ${CMAKE_SOURCE_DIR}/tests/*.algo)
foreach(algo_case ${ALGO_TEST_CASES})
    get_filename_component(case_name ${algo_case} NAME_WE)
    add_test(
        NAME ${case_name}
        COMMAND ${CMAKE_COMMAND}
                -DALGO_BIN=$<TARGET_FILE:algo>
                -DALGO_TESTS_DIR=${CMAKE_SOURCE_DIR}/tests
                -DALGO_CASE=${case_name}
                -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake
    )
    # ON THIS TIMEOUT. Item 1.2 added `while`, so a non-terminating program is
    # expressible for the first time and a case that hangs would otherwise hang
    # CI until the job's own limit — 25 minutes per case under CTest's default,
    # and the failure would read as an infrastructure problem rather than as the
    # bug it is. The guard belongs here rather than in the interpreter: an
    # iteration cap would be a branch inside the hot path of every benchmark
    # Phase 3 measures, and a language semantic the roadmap does not list.
    # Every case currently runs in about ten milliseconds, so ten seconds is
    # three orders of magnitude of headroom and can only fire on a real hang.
    set_tests_properties(${case_name} PROPERTIES TIMEOUT 10)
endforeach()
