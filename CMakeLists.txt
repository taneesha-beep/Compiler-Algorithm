cmake_minimum_required(VERSION 3.20)

project(algo)

set(CMAKE_CXX_STANDARD 20)

# The front end and engines, built once and shared. `main.cpp` stays out of it
# so that a test binary can link the same objects alongside its own main().
add_library(algo_core STATIC
    src/diagnostic.cpp
    src/lexer.cpp
    src/parser.cpp
    src/semantic.cpp
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

# Golden-file cases: run tests/<name>.algo and compare stdout against
# tests/<name>.expected.
file(GLOB ALGO_TEST_CASES ${CMAKE_SOURCE_DIR}/tests/*.algo)
foreach(algo_case ${ALGO_TEST_CASES})
    get_filename_component(case_name ${algo_case} NAME_WE)
    add_test(
        NAME ${case_name}
        COMMAND ${CMAKE_COMMAND}
                -DALGO_BIN=$<TARGET_FILE:algo>
                -DALGO_INPUT=${CMAKE_SOURCE_DIR}/tests/${case_name}.algo
                -DALGO_EXPECTED=${CMAKE_SOURCE_DIR}/tests/${case_name}.expected
                -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake
    )
endforeach()
