#!/bin/bash
# 6.4's clip: front end, back end, and the differential suite that holds the two
# engines to one set of golden files. NO TIMING IS SHOWN, deliberately -- there
# is no measurement outside the Linux container and this machine is not it, so
# ctest's own wall clock is filtered out rather than left beside two engines
# where a reader would take it for a comparison.
E=$'\033'
say()  { printf "%s[1;33m# %s%s[0m\n" "$E" "$1" "$E"; sleep 1.1; }
beat() { printf "%s[1;36m%s%s[0m $ %s\n" "$E" "${PWD##*/}" "$E" "$1"; sleep 0.6; eval "$1"; echo; sleep "${2:-1.3}"; }

cd tests
say "1. the front end -- a deliberate fault, and the diagnostic it renders"
beat 'cat error_int_overflow.algo' 0.7
beat '../build/algo error_int_overflow.algo; echo "exit $?"' 2.6

say "2. the back end -- the same front end, dumping bytecode for a while loop"
beat 'cat while_sum.algo' 0.7
beat '../build/algo --dump while_sum.algo' 3.8

cd ..
say "3. both engines -- every golden case runs twice, tree-walker and VM,"
say "   against the same .expected files. A test, not a benchmark."
beat "ctest --test-dir build -R _tree | grep 'tests passed'" 1.2
beat "ctest --test-dir build -R _vm   | grep 'tests passed'" 1.6
say "same bytes on stdout, same diagnostic on stderr, same exit code."
say "no timings here -- the numbers are cachegrind counts, taken under Linux."
sleep 1.0
