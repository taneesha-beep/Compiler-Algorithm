# Runs ALGO_BIN on ALGO_INPUT and compares stdout to ALGO_EXPECTED.
# Invoked by CTest via `cmake -P` (see add_test in CMakeLists.txt).

execute_process(
    COMMAND ${ALGO_BIN} ${ALGO_INPUT}
    OUTPUT_VARIABLE actual_output
    RESULT_VARIABLE run_result
)

file(READ ${ALGO_EXPECTED} expected_output)

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "Output mismatch for ${ALGO_INPUT}\n"
        "--- expected ---\n${expected_output}\n"
        "--- actual ---\n${actual_output}\n")
endif()
