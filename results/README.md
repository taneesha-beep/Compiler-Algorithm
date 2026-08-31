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

### Phase 3, item 3.2 — eight rows: ablation B, in both series

**Four `perf/iso-b` rows at commit `62fd024` and four `perf/cum-b` rows at
`6d7e9b0`.** Unlike A, these are *two different commits*, and that is the whole
point of the item: the isolated series applies each ablation to N alone, the
cumulative series applies them in the order A → B → C → D, and **B is where the
two stop being the same tree.** `perf/iso-b` is B on top of `v1-naive-treewalk`,
on a branch off it; `perf/cum-b` is B on top of A, on `main`. The only
non-comment difference between the two trees is ablation A's two signature
lines, which is what makes the pair readable as a controlled comparison.

There is therefore **no free reproducibility control here**, the way there was
at A where one commit carried both tags. The controls this item has are the
`output` column — unchanged at `24000000`, `2178309`, `10000000`, `136000000` in
all eight rows — and the branch model below, which is a much stronger check than
a repeat measurement.

**Ablation B, applied to N alone** (`v1-naive-treewalk` minus `perf/iso-b`):

| Program | `Ir` | | `D1_misses` | | `branches` | `mispredicts` |
|---|---|---|---|---|---|---|
| `bench/arith.algo` | −4,511,997,996 | −29.92% | +3 | +0.01% | −783,999,654 | −9,999,945 |
| `bench/fib32.algo` | −1,846,878,088 | −11.55% | −2,070,236 | −15.57% | −310,162,732 | −15,679,089 |
| `bench/loop10m.algo` | −3,739,999,872 | −26.75% | +0 | +0.00% | −719,999,978 | −39,999,983 |
| `bench/vars.algo` | −23 | −0.00% | −3 | −0.00% | +0 | −1,999,927 |

**Ablation B, applied on top of A** (`perf/cum-a` minus `perf/cum-b`) — this is
the marginal step in the cumulative series, N → A → **B**:

| Program | `Ir` | | `D1_misses` | | `branches` | `mispredicts` |
|---|---|---|---|---|---|---|
| `bench/arith.algo` | −4,497,997,996 | −34.13% | +10 | +0.05% | −783,999,654 | −7,999,959 |
| `bench/fib32.algo` | −1,829,255,203 | −12.52% | −1,704,233 | −16.37% | −310,162,732 | +2,965,610 |
| `bench/loop10m.algo` | −3,719,999,872 | −29.50% | +0 | +0.00% | −719,999,978 | −19,999,981 |
| `bench/vars.algo` | +20,999,978 | +0.18% | −4 | −0.02% | +0 | +96 |

#### The branch column pins B, and it takes two terms rather than one

Item 3.1 divided each program's branch delta by the `evaluate` calls its source
implies and got **exactly 5.0** three times. B does not admit a single per-visit
figure, and it should not: what B removed is a `std::stoll` call, whose cost has
a fixed part and a part proportional to the digits, because `strtoll` loops over
them. So the unit is a **literal evaluation**, and the model has two terms.

Solving the two coefficients on `fib32` (every literal is one digit) and
`loop10m` (an eight-digit literal per condition, a one-digit literal per body
turn) gives **18 branches + 4 per digit** and **115 instructions + 16 per
digit**, per literal evaluated. `arith` and `vars` took no part in the fit and
are predictions:

| Program | literals evaluated | digits | `branches` predicted | observed | `Ir` predicted | observed |
|---|---|---|---|---|---|---|
| `bench/fib32.algo` | 14,098,309 | 14,098,309 | 310,162,732 | 310,162,732 | 1,846,878,088 | 1,846,878,088 |
| `bench/loop10m.algo` | 20,000,001 | 90,000,008 | 719,999,978 | 719,999,978 | 3,739,999,872 | 3,739,999,872 |
| `bench/arith.algo` | 32,000,001 | 52,000,007 | 783,999,902 | 783,999,654 | 4,511,999,392 | 4,511,997,996 |

`arith` is predicted to **0.00003%** on both columns, on a program with a
different node mix, a different literal-to-node ratio and a different digit
profile from either program used to fit. The model predicts slightly *more*
removed than was observed — 248 branches and 1,396 instructions on `arith` — and
that is the right sign and the right order of magnitude for the parse-time
conversion B **adds**: eighteen literals of twenty-eight digits in that source,
which the same two coefficients price at 436 branches and 2,518 instructions,
paid once instead of thirty-two million times.

**The same two coefficients hold in the cumulative series.** B's branch delta on
top of A is identical to B's branch delta on N — `−783,999,654`,
`−310,162,732`, `−719,999,978`, `+0`, the same four numbers — so A and B remove
disjoint branch traffic.

**The literal counts are derived from the source, and the fit is what checks
them.** `fib(32)` makes 2·F(33) − 1 = 7,049,155 calls, each evaluating the `2` of
`n < 2`; its 3,524,577 non-leaf calls also evaluate the `1` and the `2` of
`fib(n - 1) + fib(n - 2)`. `arith`'s loop body holds fourteen literals in its
long expression **and a fifteenth in `i = i + 1`** — the first pass through this
arithmetic omitted that one, and the model missed `arith` by 5.6% until the
branch column forced the recount. That is the check doing its job.

#### `vars` is the control, and it works in both directions

`bench/vars.algo` evaluates **no literal at all inside its loop** — its bound is
the name `limit`, its increment is the name `one`, and its twenty declarations
are straight-line prelude. So B has nothing to remove there, and the isolated
row says so: **−23 instructions out of 12,854,819,055**, and a branch delta of
**exactly zero**. Every literal in that program is evaluated exactly once, so B
moves the conversion from evaluation time to parse time rather than removing it.

This is the strongest single statement the item can make about what B is. An
ablation that showed a large effect on a program containing none of the work it
claims to remove would be measuring something else.

#### A and B interact, and the interaction is exactly one instruction per identifier

This is the item's most interesting number and it belongs to item 5.2, which
computes the residual properly over all four ablations. It is recorded here
because it is what these rows say.

The interaction is `(N−A) + (N−B) − (N−AB)` — how much the parts over-count the
whole. In `branches` it is **exactly zero on all four programs**: A removes
reference-count traffic, B removes a `strtoll` loop, and no branch belongs to
both. In `Ir` it is positive and it is not noise:

| Program | `Ir` interaction | `IdentifierNode` evaluations the source implies | difference |
|---|---|---|---|
| `bench/arith.algo` | 14,000,000 | 14,000,001 | −1 |
| `bench/fib32.algo` | 17,622,885 | 17,622,887 | −2 |
| `bench/loop10m.algo` | 20,000,000 | 20,000,001 | −1 |
| `bench/vars.algo` | 21,000,001 | 21,000,002 | −1 |

**One instruction per identifier evaluation, on four programs whose identifier
counts span 14.0 to 21.0 million and whose node mixes have nothing in common,
each fitting to within two counts.** Neither ablation touches the identifier
arm. With the branch interaction at exactly zero, this cannot be control flow;
it is code generation — `Interpreter::evaluate` is a materially different
function in each of the four configurations, `0xfd4` bytes in N, `0xc44` in A,
`0xd70` in B and `0x938` in A+B, read with `nm -S` from the kept worktree
binaries. Removing one arm's work changes what the compiler does with the
others.

It is positive, which is the direction Phase 5 predicts: the parts sum to more
than the whole. **Item 5.2 owns the residual**; this is one pair of four
ablations, reported as an observation rather than as that result.

#### N and B do not accept the same programs

B is the only ablation in the series that changes the language, and a reader
comparing these rows should know it. The out-of-range literal check travelled
with the conversion, so it now fires on **every** literal in the file rather
than only on literals the program reaches. Exit code (65), message and caret are
unchanged — `tests/diagnostic_test.cpp` pins all three — and the change is a
strict widening: B rejects a superset of what N rejects, and every program both
accept produces identical output. **No benchmark program contains an
out-of-range literal anywhere, so no row here is affected.**
`tests/error_overflow_unreached.algo` is the case that pins the difference, and
`docs/MEASUREMENT.md`'s *Boundary of the claim* carries the argument.

#### The node grew by eight bytes, and `fib32`'s cache column shows it

B adds a `std::int64_t` to `NumberNode` and keeps the digits, so the node goes
from 48 bytes to 56 under the toolchain that took these rows (GCC 13.3.0,
libstdc++; measured with a `sizeof` probe compiled against each worktree's
`src/`). That is the only reason `bench/fib32.algo` — the program that allocates
and walks by far the most nodes — moves **−15.57%** on `D1_misses` while the
three iterative programs move by single counts. Nothing about the literal path
explains a cache effect on a program whose literals are all one digit.

This is the cost the design decision was choosing between rather than avoiding.
Dropping `text` instead of adding beside it would have taken the node to 24
bytes, a **−24**-byte change against this **+8** one, so the option taken is the
smaller perturbation of the two available — and node size is not something any
ablation in this series accounts for. See the commit message on `perf/cum-b` and
`src/ast.h`.

#### Why the ranking is not the one a reader would guess

B is largest on `arith` (−29.92%) and smallest on `vars` (nil), which is what a
literal-heavy-versus-literal-free reading predicts. `loop10m` at −26.75% ranks
*above* `fib32` at −11.55%, and that is not an anomaly: two of `loop10m`'s six
node visits per iteration are literals and one of them has eight digits, so it
is literal-dense per visit even though it is a trivial program, while `fib32`
spends most of its instructions in call and frame machinery that B does not
touch. Ablation D is the one that will show up there.

### Phase 3, item 3.3 — eight rows: ablation C, in both series

**Four `perf/iso-c` rows at commit `53da227` and four `perf/cum-c` rows at
`ef4dc25`.** The same arrangement as B, and for the same reason: from B onward
the isolated and cumulative series are different trees. `perf/iso-c` is C on top
of `v1-naive-treewalk` — **not** on top of `perf/iso-b`, which is its sibling
rather than its parent — and `perf/cum-c` is C on top of B on top of A. Checked
before the pass rather than after: `git diff v1-naive-treewalk..perf/iso-c`
shows C and nothing else, and the whole non-comment difference between the two
C trees is ablation A's two signature lines plus ablation B's conversion.

`output` is unchanged in all eight rows — `24000000`, `2178309`, `10000000`,
`136000000`. C is the first ablation since A that changes nothing a program can
observe: no diagnostic, exit code or caret moves, and the 33 existing tests are
the correctness check.

**Ablation C, applied to N alone** (`v1-naive-treewalk` minus `perf/iso-c`):

| Program | `Ir` | | `D1_misses` | | `branches` | `mispredicts` |
|---|---|---|---|---|---|---|
| `bench/arith.algo` | −113,999,864 | −0.76% | +8 | +0.04% | **+45,999,958** | +22,000,010 |
| `bench/fib32.algo` | −42,294,848 | −0.26% | −485 | −0.00% | **+24,672,025** | +3,245,013 |
| `bench/loop10m.algo` | −59,999,957 | −0.43% | −2 | −0.01% | **+19,999,994** | +18 |
| `bench/vars.algo` | −5,999,681 | −0.05% | +4 | +0.00% | **+49,999,978** | +8,000,042 |

**Ablation C, applied on top of A and B** (`perf/cum-b` minus `perf/cum-c`) —
the marginal step in the cumulative series, N → A → B → **C**:

| Program | `Ir` | | `D1_misses` | | `branches` | `mispredicts` |
|---|---|---|---|---|---|---|
| `bench/arith.algo` | −115,999,864 | −1.34% | −7 | −0.03% | **+45,999,958** | −6,000,009 |
| `bench/fib32.algo` | −49,344,002 | −0.39% | −3 | −0.00% | **+24,672,025** | +8,563,529 |
| `bench/loop10m.algo` | −69,999,957 | −0.79% | −1 | −0.00% | **+19,999,994** | +9,999,987 |
| `bench/vars.algo` | −6,999,681 | −0.06% | +2 | +0.01% | **+49,999,978** | −101 |

#### C removes instructions and *adds* branches, and the reason is in the binary

This is the item's central finding and it is not a small qualification. Every
number above is real, reproduces, and is predicted to six or seven significant
figures by the model below — but the mechanism is not the one the roadmap names
for this ablation, and the sign of the branch column is not the one a reader of
the source would expect.

**The source describes a chain of up to ten string comparisons. The compiler
does not emit one.** Disassembling `Interpreter::evaluate` in configuration N
(GCC 13.3.0, `-O2`, aarch64) shows GCC collapsed the whole chain into a
**length dispatch followed by a single-character compare chain**: it loads
`op.size()` once, branches on 1 versus 2, and then — for the six one-character
operators — loads one byte and compares it against `'+'`, `'-'`, `'*'`, `'/'`,
`'<'`, `'>'` in turn. No string comparison, no `memcmp` call, no length
recomputed per candidate.

**And the switch is not a jump table.** GCC compiled the ten-enumerator switch
into a *balanced binary decision tree* — `cmp #5` / `b.eq` / `b.gt`, then
`cmp #2` / `b.eq` / `b.gt`, and so on. The indirect-branch column confirms it:
`Bi` moves by **at most 20 counts** across the whole series, so no indirect jump
was introduced anywhere.

Counting the two dispatches instruction by instruction in the disassembly gives
this, and nothing here is fitted:

| Operator | chain position | chain `Ir` | switch `Ir` | chain branches | switch branches | `Ir` removed | branches **added** |
|---|---|---|---|---|---|---|---|
| `+` | 1 | 8 | 8 | 2 | 5 | **0** | **+3** |
| `-` | 2 | 10 | 10 | 3 | 6 | **0** | **+3** |
| `*` | 3 | 12 | 6 | 4 | 3 | **6** | −1 |
| `/` | 4 | 14 | 9 | 5 | 5 | **5** | 0 |
| `<` | 5 | 16 | 9 | 6 | 5 | **7** *(6 isolated)* | −1 |

So the effect really does track operator mix rather than operation count — but
much more sharply than "later in the chain is dearer". **A program whose only
operator is `+` gains no instructions at all from C and pays three extra
branches per operation**, because the character chain finds `+` on its first
comparison while the decision tree needs five branches to reach `Add`. Only
`*`, `/` and `<` — the operators the character chain reaches third, fourth and
fifth — come out ahead.

`evaluate` barely shrank, and that is the same statement from a third angle.
Read with `nm -S` from the kept worktree binaries, `Interpreter::evaluate` is
**`0xfd4` bytes in N and `0xfa4` in `perf/iso-c`** — 48 bytes, where ablation A
took 912 and ablation B took 612. In the cumulative arm it is `0x938` against
`0x8f4`, 68 bytes. The `0x938` is a free control on the method: item 3.2
recorded exactly that figure for A+B, and this is a different session reading
the same binary a different way.

#### The model predicts all four programs, in both series, with nothing fitted

Multiply the per-operator costs above by the operator counts each source
implies and compare against the rows. There is no free parameter: the costs
were counted in the disassembly, not solved for.

| Program | `+` | `-` | `*` | `/` | `<` | `Ir` predicted | observed | `branches` predicted | observed |
|---|---|---|---|---|---|---|---|---|---|
| `bench/arith.algo` | 12,000,000 | 8,000,000 | 12,000,000 | 6,000,000 | 2,000,001 | 114,000,006 | 113,999,864 | −45,999,999 | −45,999,958 |
| `bench/fib32.algo` | 3,524,577 | 7,049,154 | — | — | 7,049,155 | 42,294,930 | 42,294,848 | −24,672,038 | −24,672,025 |
| `bench/loop10m.algo` | 10,000,000 | — | — | — | 10,000,001 | 60,000,006 | 59,999,957 | −19,999,999 | −19,999,994 |
| `bench/vars.algo` | 17,000,000 | — | — | — | 1,000,001 | 6,000,006 | 5,999,681 | −49,999,999 | −49,999,978 |

Worst error on `branches` is **41 counts in 46 million** (0.00009%); worst on
`Ir` is 325 in 6 million (0.005%), on the program where the total is smallest.
Every residual is the same sign and the same order — tens to hundreds of counts
— which is one-time work at parse and process start, the only place the two
binaries differ off the hot path.

**This is where item 3.1's and 3.2's per-visit check had to change shape a third
time.** A priced a fixed sequence, so one constant fitted three programs. B
priced a loop over digits, so it took two terms. C prices *different work per
operator*, so no single per-operation figure exists at all, and the linear
"cost rises with chain position" model a reader would reach for first misses
`arith` by 35% and `fib32` by 29%. The instruction that resolves it is the
disassembler, not the arithmetic: the chain the source describes and the chain
the processor executes are different chains. 3.2's lesson — *when the model
misses, suspect your counting first* — extends to what is being counted.

#### C's interaction with A and B is exactly one instruction per `<` evaluation

`branches` is **identical in the two series** — `+45,999,958`, `+24,672,025`,
`+19,999,994`, `+49,999,978`, the same four numbers on top of N and on top of
A+B, difference exactly zero on all four programs. C's branch effect does not
interact with A or B at all, the same result the A×B pair gave.

In `Ir` it does interact, and cleanly:

| Program | isolated `Ir` removed | cumulative | difference | `<` evaluations | difference − (`<` − 1) |
|---|---|---|---|---|---|
| `bench/arith.algo` | 113,999,864 | 115,999,864 | 2,000,000 | 2,000,001 | **0** |
| `bench/fib32.algo` | 42,294,848 | 49,344,002 | 7,049,154 | 7,049,155 | **0** |
| `bench/loop10m.algo` | 59,999,957 | 69,999,957 | 10,000,000 | 10,000,001 | **0** |
| `bench/vars.algo` | 5,999,681 | 6,999,681 | 1,000,000 | 1,000,001 | **0** |

C is worth exactly one instruction per `<` more when A and B are already
applied, on four programs whose `<` counts span 1.0 to 10.0 million — exact, not
approximate, on every one. The mechanism is visible: the `<` arm's body is
byte-identical in the two configurations except for a `mov x2, #1` that the
compiler re-materialises inside the arm in the isolated tree and hoists out of it
in the cumulative one. One instruction, given back on the isolated side, which
is why the table above records `<` at 7 instructions removed in the cumulative
series and 6 in the isolated one.

**That is the same kind of interaction item 3.2 found, and it is again not
stalls.** A×B was one instruction per `IdentifierNode` evaluation; C×AB is one
instruction per `<` evaluation. Both are visible in instructions retired, where
nothing can hide behind a memory stall, and both are the compiler re-generating
the arms it did not change once a neighbouring arm shrinks. **Item 5.2 owns the
residual**; this is recorded here as what these rows say.

#### The node grew by eight bytes and the cache columns did not move

`BinOpNode` goes from 80 bytes to 88 under C, and `UnaryOpNode` from 64 to 72
(GCC 13.3.0, libstdc++, `sizeof` probe compiled against each tree). B's
comparable eight bytes on `NumberNode` moved `bench/fib32.algo` by −15.57% on
`D1_misses`, so the check `CLAUDE.md` requires — *look at node size before
attributing a cache movement to the ablation's named cause* — was run here
expecting to find something. It found the opposite: the largest cache movement
anywhere in these eight rows is **485 misses out of 13.3 million** on `fib32`,
which is nothing.

The reason is decisive and was measured rather than argued. Overriding
`operator new` and building a node each way, `make_shared<BinOpNode>` requests
**96 bytes before C and 104 after — and glibc's `malloc` returns a 104-byte
usable block for both.** `UnaryOpNode` is 80 against 88 requested, 88 usable
either way. The eight bytes fit inside slack the allocator was already handing
out, so **every node's heap footprint is byte-identical across C** and there is
nothing for the cache columns to move.

#### The branch predictor gets worse, and that column is a model figure

`mispredicts` rises where `branches` rises: **+20.37%** on `arith` isolated,
+4.56% on `fib32`, +3.69% on `vars`, and 18 counts on `loop10m`. In the
cumulative series it is louder and less orderly — +99.85% on `loop10m`, +21.01%
on `fib32`, −6.38% on `arith` — which is what a two-level adaptive predictor
does when the branch *pattern* changes rather than the branch count.

This is valgrind's predictor, not this machine's, exactly as the *three groups
of columns* table above says. It is reported because the direction is
consistent with the branch column and against the instruction column, not as a
claim about any real processor.

#### What C is actually worth, stated plainly

Between −0.05% and −0.76% of instructions on N, and −0.06% to −1.34% on top of A
and B. Against A (−7.04% to −12.62%) and B (−0.00% to −29.92%) that is small,
and the honest summary is that **the operator dispatch was the cheapest of the
baseline's four unforced inefficiencies by a wide margin, because the compiler
had already optimised most of it away.** The naive reading of the source — ten
string comparisons, up to ten `memcmp` calls per operation — describes work the
binary never did.

It also means C is the first ablation in the series that makes a counter go the
wrong way. Reporting the instruction win without the branch loss would be
choosing the flattering column; both are in the rows above and both belong in
item 5.1's table.
