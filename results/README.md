# `results/` — the measurement ledger

Every number that appears anywhere in this repository must be traceable to a row
in this directory. That is the rule item 2.3 exists to make possible: a figure in
the README, in `docs/`, or in a commit message is only as good as the committed
row it came from.

`measurements.csv` is that ledger. Rows are **appended and never edited**. Each
row is one binary running one program, measured once by `scripts/bench.sh`:

```bash
docker compose run --rm bench bash scripts/bench.sh build-bench/algo bench/fib32.algo --config baseline
```

The script refuses to run outside the measurement container and refuses to
measure an unoptimised build. Both refusals exist because the failure this
project cannot survive is a plausible-looking wrong number — see
[`docs/MEASUREMENT.md`](../docs/MEASUREMENT.md).

## The three groups of columns are not equally trustworthy

This is the most important thing about the file, and the schema does not enforce
it — a reader has to know it.

| Group | Columns | Status |
|---|---|---|
| **Cachegrind** | `Ir` … `mispredicts` | **Simulated, deterministic, safe to commit and to gate CI on.** These are the numbers the attribution rests on. |
| **Wall clock** | `wall_median_ms`, `wall_min_ms` | **Narrative only.** Measured inside a virtualised container on a machine whose CPU the guest cannot even name. Says whether a change is perceptible; decides nothing. **Nothing may gate CI on these.** |
| **`perf`** | `perf_cycles`, `perf_instructions`, `perf_ipc` | **Empty on this platform** and expected to stay that way. `linux-tools` is built for kernel 6.8; Docker Desktop runs LinuxKit 6.12.68, so `perf` exits 2. Hardware counts are machine-specific and are never committed as thresholds even where they do work. |

## Rows are comparable only within a platform — and only within a driver

The last five columns (`arch`, `kernel`, `compiler`, `valgrind`, `image_digest`)
exist so that a mismatch is visible in the data rather than silent. Counts taken
on different architectures are **not** comparable to each other: aarch64 and
x86_64 retire different numbers of instructions for the same work, and valgrind
models a different cache on each.

There is a second, less obvious boundary, found while validating this driver and
worth stating plainly because it bears on how the cache columns may be read.

**`Ir` is invocation-independent; the cache columns are not.** Measuring
`bench/vars.algo` against the same binary, same commit, same container:

| Launched as | `Ir` | `D1_misses` |
|---|---|---|
| `valgrind … ./build-bench/algo bench/vars.algo`, direct from the shell | 12,854,818,629 | **10,021,412** |
| the same, through an intervening `env` | 12,854,819,077 | **6,021,415** |
| `scripts/bench.sh` | 12,854,818,988 | **6,021,416** |

`Ir` spans 448 counts out of 12.85 billion — 0.0000035%, which is process
startup and nothing else. `D1_misses` takes one of *two* values differing by
exactly 4,000,000, which is exactly 4 per iteration of the program's
1,000,000-iteration loop. The discriminator is the environment block, which
shifts the initial process layout: adding a whole variable flips it, while
adding one or two bytes to an existing variable does not, and `argv[0]`'s length
does not matter at all. Each invocation style then reproduces exactly.

Two consequences:

1. **This is why the driver exists.** It fixes the invocation, so rows in this
   file are comparable to each other. A count taken by hand at a shell prompt is
   a real measurement of something, but it is not comparable to these rows and
   must not be quoted beside them.
2. **The instruction counts are the robust ones.** The attribution rests on `Ir`,
   which does not move. The cache columns are a property of valgrind's default
   model *and* of the process layout, which is one more reason they are reported
   as model-relative rather than as facts about silicon. Item 5.3 records this in
   *Boundary of the claim*.

## Schema

Fields contain no commas, quotes or newlines by construction, so the file is
readable with a bare `awk -F,`. That is deliberate: the bench image has no
`python3` and no `jq`, so `awk` is the only parser item 5.4's CI gate can use.

| Column | Meaning |
|---|---|
| `timestamp_utc` | when the row was taken |
| `config` | configuration label — a tag name in Phase 3, `baseline` for the unmodified tree-walker |
| `commit` | commit of the tree the measured binary was built from |
| `tree_state` | `clean` / `dirty` / `unknown` — `unknown` in the container, which has no `git` |
| `program` | the `.algo` program measured |
| `binary` | the interpreter measured |
| `build_type` | `CMAKE_BUILD_TYPE` read from the cache beside the binary; the driver refuses anything unoptimised |
| `output` | what the program printed — a wrong answer is visible in the row rather than hidden behind exit 0 |
| `Ir` `I1mr` `ILmr` `Dr` `D1mr` `DLmr` `Dw` `D1mw` `DLmw` `Bc` `Bcm` `Bi` `Bim` | cachegrind's thirteen raw events, in its own names |
| `D1_misses` `LL_misses` `branches` `mispredicts` | the four totals the roadmap names, derived exactly as cachegrind's own summary derives them |
| `wall_runs` `wall_median_ms` `wall_min_ms` | wall clock over `wall_runs` runs — narrative only |
| `perf_cycles` `perf_instructions` `perf_ipc` | empty on this platform |
| `arch` `kernel` `compiler` `valgrind` `image_digest` | the platform the row was taken on |

All thirteen raw events are kept, not just the five the roadmap names. One
cachegrind pass over the four benchmark programs costs about 230 seconds and
Phase 3 measures eleven configurations; dropping a counter now would mean
re-running the whole series later to recover it.

## What is in the file today

Eight rows: the four benchmark programs, each measured twice, against the
unmodified tree-walk interpreter at commit `1854f83`. The two passes agree on
**all seventeen cachegrind columns for all four programs**, which is item 2.3's
acceptance criterion.

The recorded commit is the parent of the commit that adds this file. That is
correct rather than sloppy: item 2.3 changes no source file, so the interpreter
measured is byte-for-byte the interpreter at the commit that records it.
