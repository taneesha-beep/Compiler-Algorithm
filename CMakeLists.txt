cmake_minimum_required(VERSION 3.20)

project(algo)

set(CMAKE_CXX_STANDARD 20)

add_executable(algo src/main.cpp src/lexer.cpp src/parser.cpp src/semantic.cpp src/interpreter.cpp)

enable_testing()

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