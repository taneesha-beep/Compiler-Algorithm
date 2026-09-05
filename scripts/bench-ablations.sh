#!/usr/bin/env bash
#
# scripts/bench-ablations.sh — the configuration builder (roadmap item 2.4).
#
# Measures a LIST of historical configurations. Each ref is materialised as a
# git worktree, built into a directory of its own, and handed to
# scripts/bench.sh once per benchmark program.
#
#     scripts/bench-ablations.sh main 75b84e8
#     scripts/bench-ablations.sh --dry-run perf/iso-a perf/iso-b perf/iso-c
#
# THIS SCRIPT RUNS ON THE HOST. Its primitive, scripts/bench.sh, runs only
# inside the container. That split is not a design preference — it is forced,
# and the two halves refuse in opposite directions:
#
#   * git is NOT installed in the bench image, so `git worktree add` cannot run
#     inside the container. Worktree creation is host-only.
#   * valgrind does not meaningfully support arm64 macOS, and the host's default
#     build is -O0. Measurement is container-only, and scripts/bench.sh refuses
#     anywhere else (exit 69).
#
# So SOMETHING has to cross the boundary, and this script is the only correct
# place for it. scripts/bench.sh must not: a script that re-invokes docker
# cannot itself be called from inside docker, and translating a host path into a
# container path across a bind mount that covers the repository only is the
# silently-wrong-binary failure its refusals exist to prevent. Crossing the
# boundary here confines it to one file, one line per step, spelled out by
# --dry-run before a minute of measurement is spent.
#
# FOUR THINGS ARE HELD FIXED WHILE THE INTERPRETER VARIES, and each matters:
#
#   1. The DRIVER is /repo/scripts/bench.sh — this checkout's, never the
#      worktree's. The instrument may not change between configurations, and
#      refs older than item 2.3 do not have one at all.
#   2. The PROGRAMS are /repo/bench/*.algo — this checkout's. A configuration is
#      a hypothesis about the interpreter; the stimulus is not part of it. This
#      also lets a ref that predates bench/ still be measured.
#   3. The INVOCATION is `docker compose run --rm bench bash scripts/bench.sh
#      <binary> <program> ...` — the shape that produced every committed
#      baseline row. Cachegrind's cache columns depend on how the process was
#      launched (results/README.md, docs/MEASUREMENT.md), so a second invocation
#      path would silently make the ablation deltas incomparable. Every step
#      below is passed to docker as separate argv elements; there is no shell
#      string to misquote.
#   4. The LENGTH OF THE BINARY'S OWN PATH, which is why a worktree is named
#      after its commit and not after the ref that asked for it. Item 2.3
#      recorded that the environment block moves the cache columns; measured
#      here, argv[0] does too, and by more. The same binary image measured as
#      `build-bench/algo` (16 characters) and as
#      `.worktrees/main/build-cfg/algo` (30) reports 15,000,480 and 13,560,023
#      D1 misses on bench/fib32.algo — 9.6% apart, from nothing but the length
#      of a string. `Ir` moved by 6 counts in 16 billion.
#
#      So `.worktrees/<12 hex of the commit>/build-cfg/algo` is EXACTLY 38
#      characters for every configuration, always. Naming worktrees after refs
#      would have made `perf/iso-a` and `perf/cum-a` differ in path length and
#      therefore in D1 — an ablation delta manufactured entirely by a tag name.
#      A guard below refuses the series if the lengths ever stop matching.
#
# ONE CONSEQUENCE FOR PHASE 3, and it is not optional: 38 characters is not 16,
# so rows produced here are NOT comparable, in their cache columns, to the eight
# `baseline` rows item 2.3 measured as `build-bench/algo`. Configuration N must
# be measured THROUGH THIS SCRIPT like every other configuration. `Ir` is
# comparable across both, and the attribution rests on `Ir`.
#
# The working tree is never modified. HEAD does not move, nothing is checked out
# over it, nothing is stashed. Worktrees are created detached under .worktrees/
# (gitignored) and the only file this run changes in the repository is the
# result ledger it appends to — which is the point of running it.
#
# Exit codes follow the interpreter's convention (see CLAUDE.md):
#   0  every configuration measured   64  bad command line
#   69 not the orchestration platform (in-container, or git/docker missing)
#   70 a build or a measurement failed — the run stops there
#
# It FAILS FAST. A configuration that will not build, or a program that faults
# under it, stops the series rather than leaving a gap to be noticed later. Rows
# already appended stay appended; the ledger is append-only and nothing written
# before the failure is retracted. The failing configuration's worktree is left
# where it is, --clean or not, because a build that just failed is the one thing
# in the run worth looking at.

set -euo pipefail
export LC_ALL=C

readonly EX_USAGE=64
readonly EX_UNAVAILABLE=69
readonly EX_SOFTWARE=70

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)

readonly WORKTREE_ROOT=".worktrees"
readonly BUILD_DIR="build-cfg"
readonly BINARY_NAME="algo"

usage() {
    cat <<'EOF'
usage: scripts/bench-ablations.sh [options] <ref>...

Builds and measures each git ref as a separate configuration. Runs on the HOST;
each build and each measurement happens inside the bench container.

  <ref>...            tags, branches or commits — anything git can resolve

  --programs LIST     comma-separated .algo programs from this checkout
                      (default: every bench/*.algo)
  --runs N            wall-clock runs per measurement, passed to bench.sh
  --out FILE          CSV to append to, passed to bench.sh
  --clean             remove each worktree after measuring it
  --engine E          pass --engine=E to the measured binary (tree|vm)
  --label NAME        config label for the rows, instead of the ref name (one ref only)
  --dry-run           print the plan and the exact commands, measure nothing
  -h, --help          this text

Examples:
  scripts/bench-ablations.sh --dry-run main 75b84e8
  scripts/bench-ablations.sh perf/iso-a perf/iso-b perf/iso-c perf/iso-d
  scripts/bench-ablations.sh --programs bench/fib32.algo v1-naive-treewalk

Worktrees are kept by default, under .worktrees/, so that a re-run rebuilds
nothing and the measured binary stays available for inspection. They are
gitignored and cost about a source tree plus a build directory each.

Each is named for the COMMIT it holds, not the ref that asked for it, so that
every configuration is invoked by a path of the same length: argv[0]'s length
moves cachegrind's cache columns, and a series named after its tags would report
deltas manufactured by tag names. `git worktree list` maps them back.
EOF
}

note() { printf '%s\n' "$*" >&2; }

die() {
    local code=$1; shift
    printf '\nscripts/bench-ablations.sh: %s\n' "$1" >&2; shift
    for line in "$@"; do
        if [ -z "$line" ]; then printf '\n' >&2; else printf '  %s\n' "$line" >&2; fi
    done
    printf '\n' >&2
    exit "$code"
}

# --------------------------------------------------------------------------
# Arguments
# --------------------------------------------------------------------------

refs=(); programs_arg=""; runs=""; out_file=""; clean=0; dry_run=0
engine=""; label=""

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)  usage; exit 0 ;;
        --programs) programs_arg=${2:-}; [ -n "$programs_arg" ] || die "$EX_USAGE" "--programs needs a value"; shift 2 ;;
        --runs)     runs=${2:-};        [ -n "$runs" ]        || die "$EX_USAGE" "--runs needs a value";     shift 2 ;;
        --out)      out_file=${2:-};    [ -n "$out_file" ]    || die "$EX_USAGE" "--out needs a value";      shift 2 ;;
        --engine)   engine=${2:-};      [ -n "$engine" ]      || die "$EX_USAGE" "--engine needs a value";   shift 2 ;;
        --label)    label=${2:-};       [ -n "$label" ]       || die "$EX_USAGE" "--label needs a value";    shift 2 ;;
        --clean)    clean=1; shift ;;
        --dry-run)  dry_run=1; shift ;;
        -*)         die "$EX_USAGE" "unknown option '$1'" "run with --help" ;;
        *)          refs+=("$1"); shift ;;
    esac
done

[ ${#refs[@]} -gt 0 ] || { usage >&2; exit "$EX_USAGE"; }

# --label renames the config column, which is the only place a row says which
# engine ran. With two refs it would put one name on two configurations, so it
# is refused rather than silently applied to the first.
if [ -n "$label" ] && [ ${#refs[@]} -ne 1 ]; then
    die "$EX_USAGE" "--label names one configuration, but ${#refs[@]} refs were given"
fi

# --------------------------------------------------------------------------
# Gate 1 — is this the orchestration platform?
#
# The mirror image of scripts/bench.sh's gate. That one refuses off Linux
# because measurement happens only in the container; this one refuses IN the
# container because worktree creation happens only on the host. Between them
# there is no way to run either half in the wrong place and get a number.
# --------------------------------------------------------------------------

if [ -f /.dockerenv ]; then
    die "$EX_UNAVAILABLE" \
        "refusing to run — this is the container, and this script belongs on the host." \
        "" \
        "git is not installed in the bench image, so 'git worktree add' cannot run" \
        "here. This script creates the worktrees; the container builds and measures" \
        "them. Run it from the host checkout instead:" \
        "" \
        "    scripts/bench-ablations.sh ${refs[*]}" \
        "" \
        "See docs/MEASUREMENT.md."
fi

command -v git >/dev/null 2>&1 || \
    die "$EX_UNAVAILABLE" \
        "refusing to run — git is not on PATH." \
        "" \
        "Configurations are materialised with 'git worktree add'. Without git there" \
        "is nothing to measure and nothing to name a row after."

git -C "$repo_root" rev-parse --git-dir >/dev/null 2>&1 || \
    die "$EX_UNAVAILABLE" \
        "refusing to run — $repo_root is not a git repository."

# A linked worktree cannot host worktrees of its own here: compose bind-mounts
# this directory as /repo, and a nested .worktrees/ inside an existing worktree
# would measure builds of a checkout the ledger is not describing.
if [ ! -d "$repo_root/.git" ]; then
    die "$EX_UNAVAILABLE" \
        "refusing to run — $repo_root is itself a linked worktree." \
        "" \
        "Run this from the main checkout, whose .git is a directory. Worktrees are" \
        "created beneath it and exposed to the container by the bind mount."
fi

[ -f "$repo_root/compose.yaml" ] || \
    die "$EX_UNAVAILABLE" \
        "refusing to run — no compose.yaml at $repo_root." \
        "" \
        "The bench service is what builds and measures each configuration."

[ -x "$repo_root/scripts/bench.sh" ] || \
    die "$EX_UNAVAILABLE" \
        "refusing to run — scripts/bench.sh is missing or not executable." \
        "" \
        "This script orchestrates that one; it does not measure anything itself."

if command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
    compose=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
    compose=(docker-compose)
else
    die "$EX_UNAVAILABLE" \
        "refusing to run — docker compose is not available." \
        "" \
        "Every build and every measurement happens inside the bench service." \
        "See docs/MEASUREMENT.md."
fi

if [ "$dry_run" -eq 0 ] && ! docker info >/dev/null 2>&1; then
    die "$EX_UNAVAILABLE" \
        "refusing to run — the Docker daemon is not responding." \
        "" \
        "On this machine it is not started at login. A missing socket is not a" \
        "broken install:" \
        "" \
        "    open -a Docker        # then about seventy seconds"
fi

# --------------------------------------------------------------------------
# The programs — this checkout's, never a worktree's
# --------------------------------------------------------------------------

programs=()
if [ -n "$programs_arg" ]; then
    IFS=',' read -r -a programs <<< "$programs_arg"
else
    while IFS= read -r p; do programs+=("$p"); done < <(cd -- "$repo_root" && ls -1 bench/*.algo 2>/dev/null | sort)
fi

[ ${#programs[@]} -gt 0 ] || die "$EX_USAGE" "no benchmark programs to run" "expected bench/*.algo, or pass --programs"

for p in "${programs[@]}"; do
    [ -f "$repo_root/$p" ] || die "$EX_USAGE" "no such program in this checkout: $p"
done

# --------------------------------------------------------------------------
# The refs — resolved on the host, where git exists
#
# Each row records the full SHA via bench.sh's --commit. The container reads
# .git directly and a worktree's .git is a FILE naming a host path that means
# nothing inside the container, so without this the commit column would say
# 'unknown' for every configuration in the series.
# --------------------------------------------------------------------------

shas=(); paths=()
for ref in "${refs[@]}"; do
    sha=$(git -C "$repo_root" rev-parse --verify --quiet "$ref^{commit}" || true)
    [ -n "$sha" ] || die "$EX_USAGE" \
        "no such ref: $ref" \
        "" \
        "Phase 3's tags do not exist until Phase 3 creates them."
    shas+=("$sha")
    # Named for the commit, not the ref — see note 4 in the header. Twelve hex
    # digits, so the path is the same length for every configuration, and two
    # refs naming one commit resolve to one worktree because they are one
    # configuration.
    paths+=("$WORKTREE_ROOT/${sha:0:12}")
done

# The tripwire for note 4. By construction these are all equal; the check is
# here so that a later change to WORKTREE_ROOT, BUILD_DIR or BINARY_NAME cannot
# quietly reintroduce a per-configuration path length, which would show up as a
# cache-column delta with no cause in the source.
len_first=""
for i in "${!paths[@]}"; do
    this_len=${#paths[$i]}
    this_len=$(( this_len + ${#BUILD_DIR} + ${#BINARY_NAME} + 2 ))
    if [ -z "$len_first" ]; then
        len_first=$this_len
    elif [ "$this_len" != "$len_first" ]; then
        die "$EX_SOFTWARE" \
            "the configurations would be invoked by paths of different lengths." \
            "" \
            "  $len_first characters vs $this_len characters" \
            "" \
            "argv[0]'s length moves cachegrind's cache columns — 9.6% on" \
            "bench/fib32.algo's D1 misses for a 14-character difference. A series" \
            "measured this way would report deltas manufactured by a path name." \
            "See the header of this script and docs/MEASUREMENT.md."
    fi
done

# --------------------------------------------------------------------------
# The plan
# --------------------------------------------------------------------------

note "configurations: ${#refs[@]} · programs: ${#programs[@]} · measurements: $(( ${#refs[@]} * ${#programs[@]} ))"
note "each binary is invoked by a path of $len_first characters — held equal on purpose"
note ""
for i in "${!refs[@]}"; do
    note "  ${refs[$i]}  ->  ${shas[$i]:0:12}  ->  ${paths[$i]}/$BUILD_DIR/$BINARY_NAME"
done
note ""

# Options passed straight through to bench.sh. Kept as an array so that a value
# containing a space stays one argument; `bench_shown` is the same list flattened
# for --dry-run, where it is read rather than executed.
bench_opts=(); bench_shown=""
if [ -n "$runs" ]; then
    bench_opts+=(--runs "$runs"); bench_shown="$bench_shown --runs $runs"
fi
if [ -n "$out_file" ]; then
    bench_opts+=(--out "$out_file"); bench_shown="$bench_shown --out $out_file"
fi
if [ -n "$engine" ]; then
    bench_opts+=(--engine "$engine"); bench_shown="$bench_shown --engine $engine"
fi

if [ "$dry_run" -eq 1 ]; then
    note "--dry-run: the commands that would run, in order"
    note ""
    for i in "${!refs[@]}"; do
        wt=${paths[$i]}
        note "  git worktree add --detach $wt ${shas[$i]:0:12}"
        note "  ${compose[*]} run --rm bench cmake -S $wt -B $wt/$BUILD_DIR -DCMAKE_CXX_STANDARD=20 -DCMAKE_BUILD_TYPE=RelWithDebInfo"
        note "  ${compose[*]} run --rm bench cmake --build $wt/$BUILD_DIR -j"
        for prog in "${programs[@]}"; do
            note "  ${compose[*]} run --rm bench bash scripts/bench.sh $wt/$BUILD_DIR/$BINARY_NAME $prog --config ${label:-${refs[$i]}} --commit ${shas[$i]}$bench_shown"
        done
        [ "$clean" -eq 1 ] && note "  git worktree remove --force $wt"
        note ""
    done
    note "nothing was measured and no worktree was created."
    exit 0
fi

# --------------------------------------------------------------------------
# One configuration at a time
# --------------------------------------------------------------------------

cd -- "$repo_root"

rows_file=$(mktemp)
trap 'rm -f "$rows_file"' EXIT

started_head=$(git rev-parse HEAD)

for i in "${!refs[@]}"; do
    ref=${refs[$i]}; sha=${shas[$i]}; wt=${paths[$i]}

    note "=============================================================="
    note "configuration ${ref}  (${sha:0:12})"
    note "=============================================================="

    # -- materialise ------------------------------------------------------
    #
    # Detached on purpose. A branch cannot be checked out in two worktrees at
    # once, and a configuration is a fixed commit rather than a moving ref: a
    # row saying 'main' six months from now must still name the commit it
    # measured, which is why --commit carries the SHA.

    if [ -e "$wt" ]; then
        existing=$(git -C "$wt" rev-parse HEAD 2>/dev/null || printf 'unknown')
        if [ "$existing" != "$sha" ]; then
            die "$EX_SOFTWARE" \
                "a worktree already exists at $wt and is not at $ref." \
                "" \
                "  wanted:  $sha" \
                "  found:   $existing" \
                "" \
                "Reusing it would build one configuration and label the row another." \
                "Remove it and run again:" \
                "" \
                "    git worktree remove --force $wt"
        fi
        note "worktree: reusing $wt (already at ${sha:0:12})"
    else
        note "worktree: git worktree add --detach $wt ${sha:0:12}"
        # Detached, always. A branch cannot be checked out in two worktrees at
        # once, so a bare ref name would fail here for no reason the operator
        # could act on. git's own message is passed through rather than
        # swallowed — this is where a permissions or disk problem surfaces.
        if ! wt_err=$(git worktree add --detach "$wt" "$sha" 2>&1); then
            die "$EX_SOFTWARE" \
                "git worktree add failed for $ref at $wt." \
                "" \
                "$(printf '%s' "$wt_err" | head -5)"
        fi
    fi

    # The build directory is untracked but matches .gitignore's build-*/, so a
    # clean worktree stays clean across a build. -uno keeps the check on tracked
    # files, which is what 'this is exactly that commit' means; a ref whose
    # .gitignore predates build-*/ would otherwise report its own build output.
    if [ -n "$(git -C "$wt" status --porcelain --untracked-files=no)" ]; then
        die "$EX_SOFTWARE" \
            "the worktree at $wt has modified tracked files." \
            "" \
            "A configuration must be exactly its ref. The container has no git, so" \
            "the row's tree_state column can only say 'unknown' — this check is what" \
            "stands behind that column instead."
    fi

    # -- build ------------------------------------------------------------
    #
    # Same flags as Dockerfile.bench's inline command, and they must stay the
    # same: an unoptimised build is refused by bench.sh (exit 69), and a
    # configuration built at a different optimisation level is not comparable to
    # any other configuration — -O0 costs 6.26x the instructions of -O2, which
    # would swamp every ablation delta the series exists to measure.

    note "build:    cmake -S $wt -B $wt/$BUILD_DIR -DCMAKE_BUILD_TYPE=RelWithDebInfo"
    "${compose[@]}" run --rm bench \
        cmake -S "$wt" -B "$wt/$BUILD_DIR" \
              -DCMAKE_CXX_STANDARD=20 -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null || \
        die "$EX_SOFTWARE" "cmake configure failed for $ref" \
            "" "Nothing was measured for this configuration and no row was written."

    "${compose[@]}" run --rm bench cmake --build "$wt/$BUILD_DIR" -j >/dev/null || \
        die "$EX_SOFTWARE" "the build failed for $ref" \
            "" "Nothing was measured for this configuration and no row was written."

    [ -x "$wt/$BUILD_DIR/$BINARY_NAME" ] || \
        die "$EX_SOFTWARE" \
            "the build produced no $BINARY_NAME at $wt/$BUILD_DIR/." \
            "" \
            "This ref's CMakeLists.txt may name a different target."

    # -- measure ----------------------------------------------------------
    #
    # bench.sh writes the row. Its stdout is the row; its stderr is progress and
    # is left to flow through. Its exit code is this script's: a non-zero one
    # always means no row was appended, so the failure is reported rather than
    # summarised over.

    for prog in "${programs[@]}"; do
        row=$("${compose[@]}" run --rm bench bash scripts/bench.sh \
                  "$wt/$BUILD_DIR/$BINARY_NAME" "$prog" \
                  --config "${label:-$ref}" --commit "$sha" "${bench_opts[@]+"${bench_opts[@]}"}") || {
            rc=$?
            die "$EX_SOFTWARE" \
                "measurement failed for $ref on $prog (bench.sh exited $rc)." \
                "" \
                "No row was written for it. The series stops here; rows appended" \
                "before this point stand." \
                "" \
                "bench.sh exit codes: 64 bad command line · 69 not the measurement" \
                "platform or a build that must not be measured · 70 the measurement" \
                "itself failed."
        }
        printf '%s\n' "$row" >> "$rows_file"
    done

    if [ "$clean" -eq 1 ]; then
        note "cleanup:  git worktree remove --force $wt"
        git worktree remove --force "$wt" || note "warning: could not remove $wt"
    fi
done

# --------------------------------------------------------------------------
# Summary — and the proof that the working tree did not move
# --------------------------------------------------------------------------

ended_head=$(git rev-parse HEAD)
[ "$started_head" = "$ended_head" ] || \
    die "$EX_SOFTWARE" \
        "HEAD moved during the run: ${started_head:0:12} -> ${ended_head:0:12}." \
        "" \
        "This script must never check anything out over the working tree."

note ""
note "=============================================================="
note "$(wc -l < "$rows_file" | tr -d ' ') rows appended"
note "=============================================================="
awk -F, 'BEGIN { printf "%-16s %-20s %16s %14s\n", "config", "program", "Ir", "D1_misses" }
         { printf "%-16s %-20s %16s %14s\n", $2, $5, $9, $22 }' "$rows_file" >&2
note ""
note "HEAD unchanged at ${ended_head:0:12}; working tree untouched apart from the ledger."
