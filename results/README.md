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

**The code that would fill those three columns has never executed.** `perf` has never worked here, so the branch in `scripts/bench.sh` that collects it is written but unverified. It fails safe — a value that is not a plain integer leaves the field empty rather than being recorded — but if a future platform makes `perf` work, **check that branch against a known-good `perf` run before trusting a number it produces.**

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

### And the path the binary is invoked by moves them further

Item 2.4 found the larger half of the same effect. The **length of `argv[0]`**
shifts the cache columns more than the environment block does — which matters
because a worktree build is necessarily invoked by a longer path than
`build-bench/algo`. Same binary image, same program, same container:

| Invoked as | length | `Ir` | `D1_misses` |
|---|---|---|---|
| `build-bench/algo` | 16 | 15,996,278,997 | **15,000,480** |
| `.worktrees/main/build-cfg/algo` | 30 | 15,996,278,991 | **13,560,023** |
| the worktree's own binary, copied to a 16-character path | 16 | 15,996,278,997 | **15,000,480** |

The third row holds the binary fixed and changes only the path, and it
reproduces the first **exactly on all seventeen cachegrind columns** — so this is
the path, not the build. `Ir` spans 6 counts in 16 billion.

Two consequences, both acted on rather than noted:

1. **`scripts/bench-ablations.sh` names every worktree after its commit**, never
   after the ref that asked for it, so `.worktrees/<12 hex>/build-cfg/algo` is
   exactly 38 characters for every configuration in a series. Tag-named
   worktrees would have given `perf/iso-a` and `perf/cum-a` different path
   lengths and therefore different `D1_misses` — a delta manufactured by a tag
   name. The script refuses a series whose paths are not all equal in length.
2. **`config` rows measured from a worktree are not comparable to the `baseline`
   rows in their cache columns** — 38 characters against 16. Phase 3 measures
   configuration N through the orchestrator like everything else rather than
   reusing the `baseline` rows for it. Comparing `Ir` across the two is fine.

## Schema

Fields contain no commas, quotes or newlines by construction, so the file is
readable with a bare `awk -F,`. That is deliberate: the bench image has no
`python3` and no `jq`, so inside the container `awk` is the only parser there
is. (The CI instruction-count gate that originally motivated this was cut before
item 2.4; the constraint it was chosen for outlived it, because anything reading
this file *during* a measurement run reads it from inside that image.)

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
| `wall_runs` `wall_median_ms` `wall_min_ms` | wall clock over `wall_runs` runs — narrative only. For an even number of runs the median is the mean of the two middle values, rounded; the convention is stated because the column is named for it, and it decides nothing |
| `perf_cycles` `perf_instructions` `perf_ipc` | empty on this platform |
| `arch` `kernel` `compiler` `valgrind` `image_digest` | the platform the row was taken on |

All thirteen raw events are kept, not just the five the roadmap names. One
cachegrind pass over the four benchmark programs costs about 230 seconds and
Phase 3 measures eleven configurations; dropping a counter now would mean
re-running the whole series later to recover it.

## What is in the file today

**Eight `baseline` rows** from item 2.3: the four benchmark programs, each
measured twice, against the unmodified tree-walk interpreter at commit
`1854f83`, built as `build-bench/algo`. The two passes agree on **all seventeen
cachegrind columns for all four programs**, which is item 2.3's acceptance
criterion.

Their recorded commit is the parent of the commit that adds them. That is
correct rather than sloppy: item 2.3 changes no source file, so the interpreter
measured is byte-for-byte the interpreter at the commit that records it.

**Sixteen more from item 2.4**, labelled `main` and `75b84e8`: two refs, four
programs, two passes, every one built in a git worktree by
`scripts/bench-ablations.sh`. They are the acceptance evidence for that item and
they are readable as two checks at once.

*Re-running reproduces every count.* Pass 2 is **identical to pass 1 on all
seventeen cachegrind columns for all eight (configuration, program) pairs.**

*The worktree machinery measures what it claims to.* `src/` and `CMakeLists.txt`
are byte-identical across every commit from `75b84e8` to `82a92b9`, so two refs
drawn from that range are the same interpreter reached two different ways — and
they agree on **all seventeen columns for all four programs**:

| Program | `Ir` | `D1_misses` | `LL_misses` | `mispredicts` |
|---|---|---|---|---|
| `bench/fib32.algo` | 15,996,279,019 | 13,294,533 | 13,729 | 71,104,045 |
| `bench/loop10m.algo` | 13,981,640,413 | 20,285 | 12,916 | 50,014,551 |
| `bench/arith.algo` | 15,081,690,621 | 20,609 | 13,115 | 108,015,316 |
| `bench/vars.algo` | 12,854,819,055 | 3,021,434 | 13,353 | 217,016,313 |

Compare those `Ir` values against the `baseline` rows above and they agree to
within process startup — 22 counts in 16 billion on `fib32`. Compare the
`D1_misses` and they do not, for the reason in *And the path the binary is
invoked by moves them further*: 38 characters against 16. **That is the whole
argument for measuring configuration N through the orchestrator rather than
reusing the `baseline` rows for it.**

### Phase 3, item 3.1 — twelve rows: configuration N and ablation A

**Four `v1-naive-treewalk` rows.** Configuration **N**, the naive tree-walker,
at commit `9407ca6`, measured through `scripts/bench-ablations.sh` — *not* read
off the eight `baseline` rows, for the reason stated immediately above. This is
the row set every isolated ablation is subtracted from.

They arrive with a control nobody had to arrange. `src/` and `CMakeLists.txt`
are byte-identical from `75b84e8` through `9407ca6`, so N is the same
interpreter as item 2.4's `main` and `75b84e8` configurations, reached a third
way in a later session and across a restart of the Docker daemon. It reproduces
both of them **exactly on all seventeen cachegrind columns for all four
programs**.

**Eight ablation-A rows, four labelled `perf/iso-a` and four `perf/cum-a`**, all
at commit `6036d06`. Two tags on one commit is not a mistake: the isolated
series applies each ablation to N alone and the cumulative series applies them
in the order A → B → C → D, so *at A the two series are the same tree*. They
diverge at B, where `iso-b` is N+B and `cum-b` is N+A+B.

**The commit was nevertheless measured twice, once per label, and both rows are
kept.** The reason is that item 5.1 reads the cumulative table and item 5.2 the
isolated one, each by `config` label; a `perf/cum-a` that existed only as a
footnote saying "see `perf/iso-a`" would be a special case in the one
arithmetic this project exists to perform. Eight rows cost about two and a half
extra minutes and buy a second thing as well — the two label groups are the same
binary invoked by the same 38-character path, so they are a reproducibility
control, and they agree **on all seventeen cachegrind columns for all four
programs**.

What A removed, N minus A, from the committed rows:

| Program | `Ir` | | `D1_misses` | | `branches` | `mispredicts` |
|---|---|---|---|---|---|---|
| `bench/arith.algo` | −1,904,000,150 | −12.62% | −3 | −0.01% | −430,000,030 | −5,999,992 |
| `bench/fib32.algo` | −1,385,158,921 | −8.66% | −2,881,267 | −21.67% | −296,064,500 | −33,302,021 |
| `bench/loop10m.algo` | −1,370,000,128 | −9.80% | −5 | −0.02% | −300,000,025 | −20,000,000 |
| `bench/vars.algo` | −905,000,532 | −7.04% | −3,000,007 | −99.29% | −195,000,120 | −3,000,036 |

Every `output` field is unchanged from N's — `24000000`, `2178309`,
`10000000`, `136000000` — which is the same independent check item 2.2 made of
those four answers. An ablation that changed one would be visible in the row
rather than behind an exit code.

**There is no `results/ablations.csv`, and there should not be.** The roadmap's
Phase 3 acceptance names one; these rows go here instead. A second CSV would
fork the ledger this directory exists to keep single, and would mean two
schemas to hold in step for the rest of Phase 3 and all of Phase 5. The `config`
column already carries the tag name, so *a row per configuration per benchmark
program* is satisfied exactly as written, and one `awk -F,` still reads the
whole series. **Items 5.1 and 5.2 read `measurements.csv` and filter on
`config`** — `perf/cum-*` for the attribution table, `perf/iso-*` and
`v1-naive-treewalk` for the isolated deltas and the residual.

**The `branches` column is what pins the attribution.** Divide the branch delta
by the number of `evaluate` calls each program's source implies and it is
**exactly 5.0 for all three iterative programs** — `loop10m` 30 branches per
iteration over 6 calls, `arith` 215 over 43, `vars` 195 over 39. Five is what
the removed sequence contains: the null check on the control block, the
single-threaded test on the increment, the fused release check, the
single-threaded test on the decrement, and the drop-to-zero test. The
instruction delta agrees across the same three programs at 22.1–23.2 `Ir` per
call. Nothing else in the interpreter changed, and the counters say so.
