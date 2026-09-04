cmake_minimum_required(VERSION 3.20)

project(algo)

set(CMAKE_CXX_STANDARD 20)

# Item 1.5's acceptance criterion is that the suite is clean under
# `-fsanitize=undefined`, and a criterion that can only be met by remembering a
# flag is a criterion nobody re-checks. Off by default and applied to nothing
# unless asked for:
#
#     cmake -S . -B build-ubsan -DCMAKE_CXX_STANDARD=20 -DALGO_SANITIZE=ON
#     cmake --build build-ubsan && ctest --test-dir build-ubsan
#
# It MUST stay off by default. Phase 2 measures whatever the default build
# produces, and a sanitizer silently in that build would put its own
# instrumentation into every number Phase 3 attributes to a cause.
#
# `-fno-sanitize-recover` is what makes this a test rather than a log: UBSan
# prints and carries on by default, so an undefined operation would scroll past
# in output nobody reads and CTest would report the case green. Aborting turns
# it into a non-zero exit, which is all CTest looks at.
option(ALGO_SANITIZE "Build with -fsanitize=undefined (off by default)" OFF)
if(ALGO_SANITIZE)
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=undefined -g)
    add_link_options(-fsanitize=undefined)
endif()

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

# Item 4.1's chunk format. `src/chunk.h` is header-only and nothing in
# algo_core includes it yet — item 4.2 is the first thing that will — so this
# target is also the only thing that compiles it at all. That is a reason to
# have it rather than an argument against: without a translation unit including
# the header, a syntax error in it would not be found until item 4.2, and the
# encoding it defines would not be checked at all until item 4.3 ran a program.
add_executable(chunk_test tests/chunk_test.cpp)
target_link_libraries(chunk_test PRIVATE algo_core)
add_test(NAME chunk_test COMMAND chunk_test)

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
    # Almost every case runs in about ten milliseconds, so ten seconds is three
    # orders of magnitude of headroom and can only fire on a real hang.
    set_tests_properties(${case_name} PROPERTIES TIMEOUT 10)
endforeach()

# The one case that is a workload rather than a check. `fib` is Phase 1's own
# acceptance criterion — `fib(27)` under the tree-walk interpreter — so it does
# real work and takes on the order of a second here rather than ten
# milliseconds, and it is the program Phase 3 will spend its ablation series
# measuring. The longer limit is still only a hang guard: it is not an
# assertion about how long the case should take, and no number in it is a
# measurement. A run that hits sixty seconds has stopped working, not slowed
# down.
set_tests_properties(fib PROPERTIES TIMEOUT 60)
