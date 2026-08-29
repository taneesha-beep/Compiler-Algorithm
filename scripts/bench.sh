#!/usr/bin/env bash
#
# scripts/bench.sh — the measurement driver (roadmap item 2.3).
#
# Measures ONE binary running ONE program and appends ONE row to results/measurements.csv.
#
#     docker compose run --rm bench bash scripts/bench.sh build-bench/algo bench/fib32.algo
#
# THIS SCRIPT REFUSES TO RUN OUTSIDE THE MEASUREMENT CONTAINER, and refuses to
# measure an unoptimised build. Both refusals exist for one reason: the failure
# mode this project cannot survive is a plausible-looking wrong number. Valgrind
# does not meaningfully support arm64 macOS, so on the host there is no
# cachegrind at all and the default build is -O0 — which costs 6.26x the
# instructions of the -O2 build that is actually measured (docs/MEASUREMENT.md).
# A host run that "worked" would produce a row indistinguishable in shape from a
# real one and wrong by a factor of six.
#
# It does NOT re-invoke docker on your behalf. Three reasons, in order of weight:
#
#   1. Item 2.4 measures builds inside git worktrees. Re-invoking compose would
#      have to translate a host path into a container path, and the bind mount
#      covers the repository only — so the translation would either fail or
#      silently measure a different binary than the one named. That is the exact
#      failure the refusal exists to prevent, reintroduced by the convenience.
#   2. 2.4's driver runs inside the container and calls this script once per tag.
#      A script that re-invokes docker cannot be called from inside docker.
#   3. A refusal is loud and falsifiable. An automatic re-invocation hides which
#      machine produced the number, which is the one thing a result row is for.
#
# Everything that can legitimately vary is RECORDED IN THE ROW rather than
# assumed: architecture, kernel, compiler, valgrind version and the pinned image
# digest. Counts are comparable only across rows agreeing on those.
#
# Exit codes follow the interpreter's convention (see CLAUDE.md):
#   0  a row was written        64  bad command line
#   69 not the measurement platform, or a build that must not be measured
#   70 the measurement itself failed
#
# Output format is CSV, not JSON, and that was forced rather than preferred: the
# bench image has no python3 and no jq (verified, not assumed), so awk is the
# only parser available to item 5.4's CI gate. CSV also appends without
# rewriting, which keeps every measurement ever taken in one greppable ledger
# and makes a git diff of results/ show exactly which rows a commit added.
#
# All thirteen cachegrind events are recorded, not just the five the roadmap
# names. One cachegrind pass over the four benchmark programs costs ~230 s and
# Phase 3 measures eleven configurations; dropping a counter now would mean
# re-running the whole series later to recover it.

set -euo pipefail
export LC_ALL=C

readonly EX_USAGE=64
readonly EX_UNAVAILABLE=69
readonly EX_SOFTWARE=70

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)

usage() {
    cat <<'EOF'
usage: scripts/bench.sh <binary> <program> [options]

Measures one binary running one program and appends one row to a CSV ledger.
Must be run inside the bench container; see docs/MEASUREMENT.md.

  <binary>          interpreter to measure, e.g. build-bench/algo
  <program>         .algo program to run, e.g. bench/fib32.algo

  --config NAME     configuration label for the row (default: the commit, short)
  --commit SHA      override the detected commit
  --out FILE        CSV to append to (default: results/measurements.csv)
  --runs N          wall-clock runs (default: 10; cachegrind always runs once)
  -h, --help        this text

Example:
  docker compose run --rm bench bash scripts/bench.sh \
      build-bench/algo bench/fib32.algo --config baseline
EOF
}

note() { printf '%s\n' "$*" >&2; }

die() {
    local code=$1; shift
    printf '\nscripts/bench.sh: %s\n' "$1" >&2; shift
    for line in "$@"; do
        if [ -z "$line" ]; then printf '\n' >&2; else printf '  %s\n' "$line" >&2; fi
    done
    printf '\n' >&2
    exit "$code"
}

# --------------------------------------------------------------------------
# Arguments
# --------------------------------------------------------------------------

binary=""; program=""; config=""; commit_override=""; out_file=""; runs=10

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --config)  config=${2:-};          [ -n "$config" ]          || die "$EX_USAGE" "--config needs a value"; shift 2 ;;
        --commit)  commit_override=${2:-}; [ -n "$commit_override" ] || die "$EX_USAGE" "--commit needs a value"; shift 2 ;;
        --out)     out_file=${2:-};        [ -n "$out_file" ]        || die "$EX_USAGE" "--out needs a value"; shift 2 ;;
        --runs)    runs=${2:-};            shift 2 ;;
        -*)        die "$EX_USAGE" "unknown option '$1'" "run with --help" ;;
        *)
            if   [ -z "$binary" ];  then binary=$1
            elif [ -z "$program" ]; then program=$1
            else die "$EX_USAGE" "unexpected extra argument '$1'" "run with --help"
            fi
            shift ;;
    esac
done

[ -n "$binary" ] && [ -n "$program" ] || { usage >&2; exit "$EX_USAGE"; }

case "$runs" in
    ''|*[!0-9]*) die "$EX_USAGE" "--runs must be a positive integer, got '$runs'" ;;
esac
[ "$runs" -ge 1 ] || die "$EX_USAGE" "--runs must be at least 1"

[ -f "$binary" ] || die "$EX_USAGE" "no such binary: $binary"
[ -x "$binary" ] || die "$EX_USAGE" "not executable: $binary"
[ -f "$program" ] || die "$EX_USAGE" "no such program: $program"

[ -n "$out_file" ] || out_file="$repo_root/results/measurements.csv"

# --------------------------------------------------------------------------
# Gate 1 — is this the measurement platform?
# --------------------------------------------------------------------------

kernel_name=$(uname -s)
if [ "$kernel_name" != "Linux" ]; then
    die "$EX_UNAVAILABLE" \
        "refusing to run — this is not the measurement platform." \
        "" \
        "uname -s reports '$kernel_name'. Measurement happens only inside the bench" \
        "container. Valgrind does not meaningfully support arm64 macOS, so there is" \
        "no cachegrind here, and the host's default build is unoptimised (-O0) —" \
        "6.26x the instructions of the -O2 build that is actually measured. A number" \
        "taken here would look exactly like a real one and be wrong by a factor of six." \
        "" \
        "Run it inside the container instead:" \
        "" \
        "    docker compose run --rm bench bash scripts/bench.sh $binary $program" \
        "" \
        "See docs/MEASUREMENT.md."
fi

command -v valgrind >/dev/null 2>&1 || \
    die "$EX_UNAVAILABLE" \
        "refusing to run — valgrind is not on PATH." \
        "" \
        "Cachegrind is the instrument every committed number comes from; without it" \
        "this script could only report wall clock, which is narrative and is never" \
        "committed as a threshold. There is no partial measurement here." \
        "" \
        "    docker compose run --rm bench bash scripts/bench.sh $binary $program"

# --------------------------------------------------------------------------
# Gate 2 — is this a build that may be measured?
#
# The build directory is the binary's own directory in this project's layout,
# for the container build and for item 2.4's worktree builds alike. An empty
# CMAKE_BUILD_TYPE is the -O0 default and is exactly the trap described above,
# so it is refused rather than recorded. A binary with no cache beside it cannot
# be judged; that is recorded as 'unknown' and warned about, not guessed.
# --------------------------------------------------------------------------

build_dir=$(cd -- "$(dirname -- "$binary")" && pwd)
build_type="unknown"

if [ -f "$build_dir/CMakeCache.txt" ]; then
    build_type=$(sed -n 's/^CMAKE_BUILD_TYPE:[A-Z]*=//p' "$build_dir/CMakeCache.txt" | head -1)
    [ -n "$build_type" ] || build_type="(empty)"

    case "$build_type" in
        Release|RelWithDebInfo) ;;
        *)
            die "$EX_UNAVAILABLE" \
                "refusing to measure an unoptimised build." \
                "" \
                "$build_dir/CMakeCache.txt reports CMAKE_BUILD_TYPE=$build_type." \
                "Nothing in CMakeLists.txt sets a build type, so an unset one means -O0 —" \
                "which charges the interpreter 6.26x the instructions a real build executes" \
                "and would inflate every ablation delta by optimisation the compiler simply" \
                "did not attempt. Measurements are taken at -O2 (RelWithDebInfo)." \
                "" \
                "Configure it explicitly:" \
                "" \
                "    cmake -S . -B $(basename "$build_dir") -DCMAKE_CXX_STANDARD=20 -DCMAKE_BUILD_TYPE=RelWithDebInfo" \
                "" \
                "See docs/MEASUREMENT.md, 'Optimisation level — a decision, not an inheritance'."
            ;;
    esac
else
    note "warning: no CMakeCache.txt beside $binary — build type recorded as 'unknown'."
    note "         this row cannot testify that it measured an optimised build."
fi

# --------------------------------------------------------------------------
# Provenance
#
# git is NOT installed in the bench image, so the commit is resolved by reading
# .git directly. --commit overrides it. Dirty-tree detection needs git and is
# recorded as 'unknown' when git is absent rather than assumed clean: labelling
# a modified tree with a commit is the traceability hole this column exists to
# close.
# --------------------------------------------------------------------------

resolve_commit() {
    local start=$1 dir gitdir head ref
    dir=$start
    while [ "$dir" != "/" ]; do
        if [ -e "$dir/.git" ]; then break; fi
        dir=$(dirname -- "$dir")
    done
    [ -e "$dir/.git" ] || { printf 'unknown'; return; }

    if [ -d "$dir/.git" ]; then
        gitdir="$dir/.git"
    else
        # A worktree's .git is a file: "gitdir: /path/to/.git/worktrees/<name>"
        gitdir=$(sed -n 's/^gitdir: //p' "$dir/.git" | head -1)
    fi
    [ -n "$gitdir" ] && [ -d "$gitdir" ] || { printf 'unknown'; return; }

    head=$(cat "$gitdir/HEAD" 2>/dev/null || printf '')
    case "$head" in
        ref:*)
            ref=${head#ref: }
            if   [ -f "$gitdir/$ref" ]; then cat "$gitdir/$ref"
            elif [ -f "$gitdir/commondir" ] && \
                 [ -f "$gitdir/$(cat "$gitdir/commondir")/$ref" ]; then
                 cat "$gitdir/$(cat "$gitdir/commondir")/$ref"
            elif [ -f "$gitdir/packed-refs" ]; then
                 awk -v r="$ref" '$2 == r { print $1; found=1; exit } END { if (!found) print "unknown" }' \
                     "$gitdir/packed-refs"
            else printf 'unknown'
            fi ;;
        '') printf 'unknown' ;;
        *)  printf '%s' "$head" ;;     # detached HEAD holds the SHA itself
    esac
}

if [ -n "$commit_override" ]; then
    commit=$commit_override
else
    commit=$(resolve_commit "$build_dir" | tr -d '[:space:]')
    [ -n "$commit" ] || commit="unknown"
fi

if command -v git >/dev/null 2>&1 && git -C "$build_dir" rev-parse --git-dir >/dev/null 2>&1; then
    if [ -z "$(git -C "$build_dir" status --porcelain 2>/dev/null)" ]; then
        tree_state="clean"
    else
        tree_state="dirty"
    fi
else
    tree_state="unknown"
fi

[ -n "$config" ] || config=$(printf '%s' "$commit" | cut -c1-12)

arch=$(uname -m)
kernel=$(uname -r)
compiler=$(c++ --version 2>/dev/null | head -1 || printf 'unknown')
valgrind_version=$(valgrind --version 2>/dev/null || printf 'unknown')
image_digest=$(sed -n 's/^FROM .*@//p' "$repo_root/Dockerfile.bench" 2>/dev/null | head -1)
[ -n "$image_digest" ] || image_digest="unknown"
timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

# --------------------------------------------------------------------------
# Wall clock — narrative only, never a threshold
#
# Measured inside a virtualised container on a machine whose CPU the guest
# cannot even name (docs/MEASUREMENT.md). It is here to say whether a change is
# perceptible, not to decide anything. Nothing may gate CI on these columns.
#
# Every run's stdout is compared against the first. A binary that crashes
# instantly produces a fast, plausible row; a program that prints a different
# answer each time is not a benchmark. Both are refused.
# --------------------------------------------------------------------------

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

note "measuring $config: $binary $program"
note "  wall clock: $runs runs"

times=""
expected_out=""
for i in $(seq 1 "$runs"); do
    start_ns=$(date +%s%N)
    if ! "$binary" "$program" >"$tmp/run.out" 2>"$tmp/run.err"; then
        die "$EX_SOFTWARE" \
            "the program failed on wall-clock run $i — no row written." \
            "" \
            "$binary $program exited non-zero. stderr:" \
            "" \
            "$(head -5 "$tmp/run.err" 2>/dev/null)"
    fi
    end_ns=$(date +%s%N)
    times="$times $(( (end_ns - start_ns) / 1000000 ))"

    if [ "$i" -eq 1 ]; then
        expected_out=$(cat "$tmp/run.out")
    elif [ "$(cat "$tmp/run.out")" != "$expected_out" ]; then
        die "$EX_SOFTWARE" \
            "the program printed different output on run $i — no row written." \
            "" \
            "run 1: $expected_out" \
            "run $i: $(cat "$tmp/run.out")" \
            "" \
            "A program whose output varies between runs is not a benchmark."
    fi
done

read -r wall_min wall_median <<EOF
$(printf '%s\n' $times | sort -n | awk '
    { a[NR] = $1 }
    END { n = NR
          if (n % 2) m = a[(n + 1) / 2]
          else       m = int((a[n / 2] + a[n / 2 + 1]) / 2 + 0.5)
          print a[1], m }')
EOF

note "  wall clock: median ${wall_median} ms, min ${wall_min} ms"

# --------------------------------------------------------------------------
# Cachegrind — simulated, deterministic, and the only counts safe to commit
#
# Parsed from the out-file rather than the stderr summary: the out-file's
# 'summary:' line is unformatted, fixed-order and names its own event order in a
# preceding 'events:' line, so it is read by name rather than by position. The
# image has no python3, so cg_annotate cannot run — see docs/MEASUREMENT.md.
#
# The container's default command writes to /dev/null; a run that wants counts
# must give a real path, which is what this does.
#
# Fields are carried through awk as strings and summed in bash. Ubuntu's awk is
# mawk, whose integer printf is 32-bit, and these counts run to ten billion.
# --------------------------------------------------------------------------

note "  cachegrind: 1 run (roughly a minute)"

if ! valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes \
        --cachegrind-out-file="$tmp/cg.out" \
        "$binary" "$program" >"$tmp/cg.stdout" 2>"$tmp/cg.stderr"; then
    die "$EX_SOFTWARE" \
        "the program failed under cachegrind — no row written." \
        "" \
        "$(tail -5 "$tmp/cg.stderr" 2>/dev/null)"
fi

if [ "$(cat "$tmp/cg.stdout")" != "$expected_out" ]; then
    die "$EX_SOFTWARE" \
        "the program printed a different answer under cachegrind — no row written." \
        "" \
        "native:     $expected_out" \
        "cachegrind: $(cat "$tmp/cg.stdout")"
fi

read -r Ir I1mr ILmr Dr D1mr DLmr Dw D1mw DLmw Bc Bcm Bi Bim <<EOF
$(awk '
    /^events:/  { for (i = 2; i <= NF; i++) f[$i] = i; next }
    /^summary:/ { for (e in f) v[e] = $(f[e]) ""; got = 1; exit }
    END { if (!got) exit 1
          print v["Ir"], v["I1mr"], v["ILmr"], v["Dr"], v["D1mr"], v["DLmr"], \
                v["Dw"], v["D1mw"], v["DLmw"], v["Bc"], v["Bcm"], v["Bi"], v["Bim"] }
' "$tmp/cg.out")
EOF

[ -n "${Bim:-}" ] || die "$EX_SOFTWARE" \
    "could not read a summary line out of the cachegrind out-file — no row written." \
    "" \
    "Expected 'events:' and 'summary:' records; see docs/MEASUREMENT.md."

# The four totals the roadmap names, derived exactly as cachegrind's own summary
# derives them (verified field by field against its printed output).
D1_misses=$(( D1mr + D1mw ))
LL_misses=$(( ILmr + DLmr + DLmw ))
branches=$(( Bc + Bi ))
mispredicts=$(( Bcm + Bim ))

note "  cachegrind: Ir $Ir · D1 misses $D1_misses · LL misses $LL_misses · mispredicts $mispredicts"

# --------------------------------------------------------------------------
# perf — optional, and absent here
#
# perf does not work on this platform and that is recorded rather than worked
# around: linux-tools is built for kernel 6.8 and Docker Desktop runs LinuxKit
# 6.12.68, so the wrapper exits 2. The three columns stay EMPTY. Nothing is
# substituted for them, and perf counts are hardware-specific and are never
# committed as thresholds even where they do work.
#
# THE BRANCH BELOW HAS NEVER EXECUTED ON THIS PLATFORM. It is written to fail
# safe — a value that is not a plain integer leaves the field empty rather than
# being recorded — and must be verified against a real perf before any number it
# produces is trusted.
# --------------------------------------------------------------------------

perf_cycles=""; perf_instructions=""; perf_ipc=""

if command -v perf >/dev/null 2>&1 && perf stat true >/dev/null 2>&1; then
    note "  perf: available — collecting cycles and instructions"
    if perf stat -x, -e cycles,instructions "$binary" "$program" \
            >/dev/null 2>"$tmp/perf.csv"; then
        perf_cycles=$(awk -F, '$3 == "cycles"       { print $1; exit }' "$tmp/perf.csv")
        perf_instructions=$(awk -F, '$3 == "instructions" { print $1; exit }' "$tmp/perf.csv")
        case "$perf_cycles"       in ''|*[!0-9]*) perf_cycles="" ;; esac
        case "$perf_instructions" in ''|*[!0-9]*) perf_instructions="" ;; esac
        if [ -n "$perf_cycles" ] && [ -n "$perf_instructions" ] && [ "$perf_cycles" -gt 0 ]; then
            perf_ipc=$(awk -v i="$perf_instructions" -v c="$perf_cycles" 'BEGIN { printf "%.3f", i / c }')
        fi
    fi
else
    note "  perf: unavailable on this platform — those three columns stay empty"
fi

# --------------------------------------------------------------------------
# The row
#
# Fields are stripped of commas, quotes and newlines so that the file is
# readable with a bare `awk -F,` — item 5.4's CI gate has no jq and no python3.
# --------------------------------------------------------------------------

csv() { printf '%s' "$1" | tr -d '\r\n"' | tr ',' ';'; }

output_field=$(printf '%s' "$expected_out" | head -1 | cut -c1-64)

header='timestamp_utc,config,commit,tree_state,program,binary,build_type,output'
header="$header,Ir,I1mr,ILmr,Dr,D1mr,DLmr,Dw,D1mw,DLmw,Bc,Bcm,Bi,Bim"
header="$header,D1_misses,LL_misses,branches,mispredicts"
header="$header,wall_runs,wall_median_ms,wall_min_ms"
header="$header,perf_cycles,perf_instructions,perf_ipc"
header="$header,arch,kernel,compiler,valgrind,image_digest"

row="$(csv "$timestamp"),$(csv "$config"),$(csv "$commit"),$(csv "$tree_state"),"
row="$row$(csv "$program"),$(csv "$binary"),$(csv "$build_type"),$(csv "$output_field"),"
row="$row$Ir,$I1mr,$ILmr,$Dr,$D1mr,$DLmr,$Dw,$D1mw,$DLmw,$Bc,$Bcm,$Bi,$Bim,"
row="$row$D1_misses,$LL_misses,$branches,$mispredicts,"
row="$row$runs,$wall_median,$wall_min,"
row="$row$perf_cycles,$perf_instructions,$perf_ipc,"
row="$row$(csv "$arch"),$(csv "$kernel"),$(csv "$compiler"),$(csv "$valgrind_version"),$(csv "$image_digest")"

mkdir -p -- "$(dirname -- "$out_file")"
if [ ! -s "$out_file" ]; then
    printf '%s\n' "$header" > "$out_file"
fi
printf '%s\n' "$row" >> "$out_file"

note "  appended to ${out_file#"$repo_root"/}"

printf '%s\n' "$row"
