# Runs one golden-file case and compares what came back.
# Invoked by CTest via `cmake -P` (see add_test in CMakeLists.txt).
#
# Inputs:
#   ALGO_BIN        absolute path to the built interpreter
#   ALGO_TESTS_DIR  absolute path to this directory
#   ALGO_CASE       the case name, e.g. "error_undef"
#
# Compared:
#   <case>.expected       stdout — required
#   <case>.expected_err   stderr — optional
#   <case>.expected_code  exit code — optional
#
# The two optional comparisons are skipped when the file is absent. Absent
# means "do not check", not "expect empty": a case may legitimately not care
# what its exit code is, and an empty .expected_err is a different assertion
# from no .expected_err at all.
#
# ON THE WORKING DIRECTORY. The interpreter is run from ALGO_TESTS_DIR and
# handed a bare filename, so the path it prints in a diagnostic is
# "error_undef.algo" and not the absolute path that CMAKE_SOURCE_DIR would
# give. That absolute path contains the checkout's location, which differs on
# every machine, so a .expected_err file recording it would pass here and fail
# in CI. Normalising the captured text afterwards would work too, but then the
# golden file would hold bytes the program never printed, and a genuine bug in
# how the path is rendered could be edited away by the substitution.

set(case_algo ${ALGO_CASE}.algo)
set(case_stem ${ALGO_TESTS_DIR}/${ALGO_CASE})

execute_process(
    COMMAND ${ALGO_BIN} ${case_algo}
    WORKING_DIRECTORY ${ALGO_TESTS_DIR}
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE actual_code
)

function(compare_or_fail what actual expected)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR
            "${what} mismatch for ${case_algo}\n"
            "--- expected ---\n${expected}\n"
            "--- actual ---\n${actual}\n")
    endif()
endfunction()

file(READ ${case_stem}.expected expected_output)
compare_or_fail("stdout" "${actual_output}" "${expected_output}")

if(EXISTS ${case_stem}.expected_err)
    file(READ ${case_stem}.expected_err expected_error)
    compare_or_fail("stderr" "${actual_error}" "${expected_error}")
endif()

if(EXISTS ${case_stem}.expected_code)
    file(READ ${case_stem}.expected_code expected_code)
    # The golden file ends in a newline; the captured code does not.
    string(STRIP "${expected_code}" expected_code)
    compare_or_fail("exit code" "${actual_code}" "${expected_code}")
endif()
