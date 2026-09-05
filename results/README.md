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

### Phase 3, item 3.4 — eight rows: ablation D, in both series

**Four `perf/iso-d` rows at commit `f194531` and four `perf/cum-d` rows at
`f1b040c`.** The same arrangement as B and C: `perf/iso-d` is D on top of
`v1-naive-treewalk` — **not** on top of `perf/iso-c`, which is its sibling
rather than its parent — and `perf/cum-d` is D on top of C on top of B on top
of A. **`perf/cum-d` is configuration H**, the hardened tree-walker, and Phase
3 ends here.

Checked before the pass rather than after, with comments stripped from both
sides: `git diff v1-naive-treewalk..perf/iso-d -- src/ tests/` is ablation D
and nothing else, and the whole non-comment difference between the two D trees
is A's two signature lines, B's conversion and node field, and C's two enums
and their two `switch`es — **with D's own seven lines byte-identical in both
trees**, which is what proves the same D was applied to both arms.

`output` is unchanged in all eight rows — `24000000`, `2178309`, `10000000`,
`136000000`. D is a representation change: no diagnostic, exit code or caret
moves, and no test was added. The 33 existing cases are the correctness check
(32 on the isolated tree, which predates B's).

**Ablation D, applied to N alone** (`v1-naive-treewalk` minus `perf/iso-d`):

| Program | `Ir` | | `D1_misses` | | `branches` | `mispredicts` |
|---|---|---|---|---|---|---|
| `bench/arith.algo` | −4,156,001,429 | −27.56% | −8 | −0.04% | −1,146,000,289 | −22,000,089 |
| `bench/fib32.algo` | −4,356,476,173 | −27.23% | **+4,341,619** | **+32.66%** | −937,274,052 | −15,909,503 |
| `bench/loop10m.algo` | −4,910,000,703 | −35.12% | −7 | −0.03% | −1,270,000,143 | −20,000,056 |
| `bench/vars.algo` | **−9,812,015,887** | **−76.33%** | −3,000,056 | −99.29% | −2,827,003,339 | −210,000,278 |

**Ablation D, applied on top of A, B and C** (`perf/cum-c` minus `perf/cum-d`)
— the last marginal step in the cumulative series, N → A → B → C → **D** = H:

| Program | `Ir` | | `D1_misses` | | `branches` | `mispredicts` |
|---|---|---|---|---|---|---|
| `bench/arith.algo` | −4,160,001,433 | −48.58% | −3 | −0.01% | −1,146,000,289 | −30,000,068 |
| `bench/fib32.algo` | −4,300,082,930 | −33.77% | **+3,161,355** | **+36.30%** | −937,274,052 | −7,666,416 |
| `bench/loop10m.algo` | −4,900,000,703 | −55.55% | −7 | −0.03% | −1,270,000,143 | −29 |
| `bench/vars.algo` | **−9,814,015,980** | **−82.03%** | −54 | −0.25% | −2,827,003,339 | −209,000,373 |

**D is the largest single effect in the series by a wide margin, which is what
the roadmap predicted for it.** Its smallest figure (−27.23% on `fib32`) is
close to B's largest (−29.92% on `arith`), and its largest is −76.33% isolated
and −82.03% cumulative. Against A (−7.04% to −12.62%) and C (−0.05% to −0.76%)
it is not a close contest. This is the only prediction the roadmap made about
an ablation's *size* that the measurement confirmed.

#### It is a figure about a 20-name frame, and that is not a quibble

`bench/vars.algo` declares twenty variables, and what D removes scales with how
deep the map is and how expensive its keys are to compare. **−76.33% is a
property of that program, not of Algo.** `bench/loop10m.algo`, whose frame holds
one name, gives −35.12% for the same ablation; `bench/fib32.algo`, one parameter
per call frame, gives −27.23%. Item 2.2 chose twenty deliberately so that D
would have something to attribute, and `docs/MEASUREMENT.md`'s *Boundary of the
claim* has said since then that D's number must be quoted with the frame width
attached. It still must.

Note also that the ranking is not monotone in frame width: `loop10m` (1 name)
shows a *larger* fraction than `arith` (2 names), because `loop10m`'s loop body
is so thin that the map is a bigger share of a smaller total. The per-lookup
cost is what rises with depth; the percentage is that cost divided by whatever
else the program does.

#### The bracket item 2.2 left open is resolved, and D is above its top end

`docs/MEASUREMENT.md` recorded that "map machinery plus string-key comparison is
somewhere between **23% and 56%** of `bench/vars.algo`'s instructions", the
spread being how much of `bcmp` served map keys rather than the operator
chain, and said **ablation D is itself the instrument that resolves it.**

It resolves to **76.33%**, which is above the bracket rather than inside it, and
the reason is instructive. The bracket was built by reading cachegrind's
per-function totals and adding up the functions whose *names* are the map. But
`std::map::find` is **inlined into `Interpreter::evaluate`**, so its descent
loop is charged to `evaluate` and never appeared in the bracket at all; only the
out-of-line `map::operator[]` and `bcmp` did. Item 3.3's lesson — *the program
you model from the source is not the program that ran* — applies to attribution
by function name just as much as to attribution by source text.

#### The model is a decomposition, and it has no fitted parameter

Ablation A priced a fixed sequence, so one constant fitted three programs.
Ablation B priced a loop over digits, so it took two terms. Ablation C priced
different work per operator, so it took a table read out of `objdump`. **D
admits no per-comparison formula at all**, and the instrument that works is
cachegrind's own per-function profile — a diagnostic run at a different
invocation, which is legitimate here because `Ir` is invocation-stable to a few
hundred counts in billions while the cache columns are not.

Every instruction D removes is in one of five places, and three of them go to
**exactly zero**:

| function | `bench/arith.algo` | `bench/loop10m.algo` | `bench/vars.algo` |
|---|---|---|---|
| `bcmp` (the string key comparison) | 1,392,001,885 → **0** | 1,400,001,369 → **0** | 4,264,009,661 → **0** |
| `std::map::operator[]` | 1,246,000,178 → **0** | 1,540,000,154 → **0** | 2,468,000,363 → **0** |
| `Interpreter::evaluate` (the inlined `find`) | −882,000,126 | −1,040,000,104 | −1,906,000,277 |
| `Interpreter::executeStatement` | −252,000,061 | −530,000,020 | −198,001,307 |
| PLT stubs for the two calls above | −384,000,092 | −400,000,036 | −976,001,148 |

`bench/fib32.algo` adds two more that only it exercises, because it is the only
program that pushes a frame per call: `_Rb_tree::_M_emplace_hint_unique`
387,703,525 → **0** and the rest of `_Rb_tree` 190,327,185 → **0**.

The two instruments that produced this, both reusable: a **counting comparator** —
`std::map<K, V, Counting>` calls its comparator exactly once per node visited, so it
measures the descent depth per key without anyone reasoning about the tree's shape — and
a **per-function cachegrind diff** of the two configurations. Neither is arithmetic on the
source, which is what the previous three ablations used and what fails here.

**The sharp check is `bcmp`, and it is exact.** Divide the `bcmp` instructions D
removes by the key comparisons each source implies:

| Program | key comparisons | `bcmp` `Ir` removed | per comparison |
|---|---|---|---|
| `bench/loop10m.algo` | 100,000,008 | 1,400,001,369 | **14.00** |
| `bench/fib32.algo` (environment only) | 70,491,548 | 986,881,672 | **14.0000** |

Both programs compare one-byte keys and nothing else — `i` against `i`, `n`
against `n` — and both give **exactly fourteen instructions per comparison**,
`fib32` to the count and `loop10m` to 1,257 counts of process startup.
`bench/arith.algo` and `bench/vars.algo` mix key lengths and outcomes and
average 16.98 and 16.34, which is the right direction and cannot be resolved
further: glibc's `bcmp` costs different amounts for the same length depending
on operand alignment, so no exact per-comparison table exists to be written.
**Saying so is the finding.** A fitted two-term model in lookups and comparisons
predicts `arith` 10.6% low and `vars` 43% high, and reporting either would be
inventing a mechanism.

#### D's falsifying control is the map it deliberately did not remove

Every ablation needs a measurement that should show nothing. D's is unusual and
stronger than a null program: **after D, `bcmp` is exactly the function table
and nothing else.**

`Interpreter::functions` is a second `std::map<std::string, …>` looked up by
name, and D deliberately left it alone — it is not what the roadmap defines D
against, and converting it would have folded an unaccounted change into D's tag.
Three of the four benchmark programs declare no function, and their `bcmp` in
configuration D is **exactly 0**. `bench/fib32.algo` declares one, and its
surviving `bcmp` is **253,770,945**. Its calls make
7,049,155 × 2 = 14,098,310 comparisons of the three-character name `fib`, which
at eighteen instructions each is 253,769,580 — the observed figure minus 1,365,
and 1,365 is the same process-startup constant the loader contributes elsewhere.

So the residual is not merely small; it is *predicted to five significant
figures by the one string-keyed lookup that was left in place on purpose*. An
ablation that had removed something other than the environment's key
comparisons could not produce that.

#### `vars` loses 99.29% of its D1 misses — and A had already taken them

Item 3.1 removed **99.29%** of `bench/vars.algo`'s D1 misses, and
`docs/MEASUREMENT.md` recorded the consequence: *ablation D should not be
expected to show a large D1 effect once A is already applied.* Both halves are
now measured and both hold exactly.

| | `bench/vars.algo` `D1_misses` |
|---|---|
| N | 3,021,434 |
| N + D (isolated) | 21,378 — **−99.29%** |
| N + A + B + C | 21,425 |
| N + A + B + C + D (**H**) | 21,371 — **−54, or −0.25%** |

**A and D independently remove the same three million misses.** In the isolated
arm D takes them; in the cumulative arm A has already taken them and D finds
fifty-four. This is the first cache-column interaction the series has produced,
and it is total rather than partial.

The allocation arithmetic says why, and it was measured rather than argued —
overriding `operator new` and reading `malloc_usable_size`. A twenty-name frame
as a `std::map` is **twenty separate allocations of 80 bytes requested and 88
usable**, one per name, scattered wherever glibc had room. The same frame as a
`std::vector<Value>` is **one allocation of 320 requested and 328 usable**, and
every slot is on a line its neighbours share.

#### But `fib32`'s D1 misses go UP by a third, and the cause is a size class

This is D's version of ablation C's branch column: a counter that moves the
wrong way, and it belongs in the record beside the win.

`bench/fib32.algo` is the only program that pushes and pops a frame per call —
7,049,155 of them — and its `D1_misses` rise from 13,294,533 to **17,636,152**,
+32.66% isolated and +36.30% cumulative. D *reduces* fib32's data traffic by a
quarter at the same time (`Dr` + `Dw` from 7,557,008,870 to 5,596,015,408), so
the miss rate per thousand accesses rises from 1.76 to 3.15.

Per function, the map's own misses go away — `map::operator[]` −1,448,348,
`_Rb_tree::_M_emplace_hint_unique` −608,415, `bcmp` −227,828 — and more than
that reappears in `Interpreter::callFunction` (+2,551,914) and
`Interpreter::executeStatement` (+2,261,354), the two functions that now touch
the frame directly.

**The mechanism is glibc's size classes, and it was measured.** In N a call
allocates its `arguments` vector (16 bytes) and its frame's tree node (80
bytes): two different size classes, two free lists. In D it allocates
`arguments` (16 bytes) and the frame vector (16 bytes): **one size class, one
tcache bin, and the two interleave.** Walking `fib(24)`'s call tree — 150,049
calls — under each allocation pattern and counting distinct addresses the frame
allocation is ever handed:

| pattern | distinct frame blocks | distinct argument blocks |
|---|---|---|
| N — 16 bytes + 80 bytes | **24** | 24 |
| D — 16 bytes + 16 bytes | **47** | 47 |

One block per live recursion depth in N, roughly two in D. The frame block got
*smaller* — 88 usable bytes to 24 — and the number of distinct lines a call
touches roughly doubled, which is what the counter shows.

**This is not something to fix.** Padding the frame vector into another size
class would be a second change wearing D's tag, and no row would attribute it.
It is recorded as a boundary in `docs/MEASUREMENT.md`, and it does not touch the
attribution: `Ir` is what that rests on, 17.6 million misses stand against 11.6
billion instructions, and the cache columns are properties of valgrind's model
in any case.

#### D changes no node, and this is the check `CLAUDE.md` requires

Item 3.3 narrowed the rule to *check the allocated block, not `sizeof`*. Here
both answers are trivial and were confirmed anyway: `src/ast.h` is
**comment-identical** across D — the only textual difference is a note added to
`unresolvedSlot` — and a `sizeof` probe compiled against each worktree returns
the same fifteen numbers on both sides, `NumberNode` 48, `IdentifierNode` 56,
`BinOpNode` 80, `Value` 16, and the rest. The slots D reads have been on the
nodes since item 1.3, so there was nothing to grow.

What did change size is the environment itself: `sizeof` goes from 48 for the
`std::map` to 24 for the `std::vector<Value>`, so the `frames` stack reserved
once at `maxCallDepth + 1` falls from 48,048 bytes to 24,024.

#### The interaction: zero in branches for the third time, mixed in `Ir`

**`branches` is identical in D's two series on all four programs** —
−1,146,000,289, −937,274,052, −1,270,000,143, −2,827,003,339 on top of N and on
top of A+B+C, **difference exactly zero everywhere.** That is the third
consecutive pair to give exactly zero branch interaction, after A×B and
C×(A+B). Three makes it a pattern rather than a coincidence: these four
ablations remove disjoint branch traffic, and the branch column decomposes
additively across all of them.

In `Ir` the third pair does **not** repeat the first two. A×B was one
instruction per `IdentifierNode` evaluation and C×(A+B) one per `<` evaluation,
both positive on all four programs. D×(A+B+C) is mixed in sign and larger:

| Program | interaction (`Ir`) | as a share of D's own delta |
|---|---|---|
| `bench/arith.algo` | **−4,000,004** | −0.10% |
| `bench/fib32.algo` | **+56,393,243** | +1.29% |
| `bench/loop10m.algo` | **+10,000,000** | +0.20% |
| `bench/vars.algo` | **−2,000,093** | −0.02% |

`fib32`'s is 8.00 instructions per call to within three counts
(7,049,155 × 8 = 56,393,240), which is the frame construction and destruction
being generated differently once the surrounding arms have shrunk. The other
three are within about one instruction per assignment executed and go both
ways. **Two of the four are negative**, meaning D is worth slightly *more* after
A, B and C than it is alone — the opposite sign from the residual Phase 5
predicts, on those two programs, at about a tenth of a percent.

**Item 5.2 owns the residual** and must compute it over all four ablations;
this is recorded here as what these rows say, and as a caution that the
pairwise interactions are not all of one sign.

#### The function sizes, for the fourth time

`Interpreter::evaluate` read with `nm -S` from the kept worktree binaries.
D is the first ablation to shrink all three of the interpreter's functions,
because it is the only one that touches all three:

| configuration | `evaluate` | `executeStatement` | `callFunction` |
|---|---|---|---|
| N `9407ca6` | `0xfd4` | `0xb20` | `0x924` |
| `perf/iso-d` `f194531` | `0xd64` | `0xa70` | `0x828` |
| `perf/cum-c` `ef4dc25` | `0x8f4` | `0x53c` | `0x7d8` |
| `perf/cum-d` `f1b040c` (**H**) | `0x750` | `0x414` | `0x6f0` |

The `0x8f4` for `perf/cum-c` reproduces the figure item 3.3 recorded, which is a
free control on the method: a different session reading the same binary.

#### Wall clock at H, which is narrative only and still worth writing down

Every program is now well below item 2.2's 0.5–5 s band, which it sat inside at
configuration N:

| Program | N | H (`perf/cum-d`) | |
|---|---|---|---|
| `bench/arith.algo` | 849 ms | **203 ms** | 4.2× |
| `bench/fib32.algo` | 869 ms | **431 ms** | 2.0× |
| `bench/loop10m.algo` | 660 ms | **175 ms** | 3.8× |
| `bench/vars.algo` | 900 ms | **95 ms** | 9.5× |

The band was item 2.2's acceptance criterion for the *baseline* programs, and
hardening the interpreter was always going to fall out of it. Nothing is
invalidated — the wall-clock columns are narrative only and cachegrind's counts
are exact and deterministic — but a Phase 4 session should know that the VM will
be measured on programs that run in tenths of a second, and should not read that
as a reason to raise any `n`. `CLAUDE.md` forbids that for a separate reason:
one cachegrind pass over the four costs about 230 seconds.

### Phase 5, item 5.1 — the attribution table: eight rows for configuration V

**Configuration V is `main` at `61996b0` run with `--engine=vm`.** It is not a commit of
its own — the VM is a flag on the same binary that carries the tree-walker — so 5.1 had to
settle how to measure it before measuring anything. The four decisions it took are below,
then the tables.

#### How V was measured, and why the driver grew a parameter

`scripts/bench.sh` invoked its binary as `$binary $program`, with no way to pass a flag.
Measuring V would then have run the **tree-walker** and labelled the row `V`, which is the
one failure this project exists to refuse. So the driver gained `--engine E`, which inserts
`--engine=E` before the program and **refuses any value but `tree` or `vm`** — the `config`
column is the only place a row says which engine ran, and a value the binary would reject
with exit 64 must not reach a run whose label claims otherwise. Empty means no flag at all,
byte for byte the invocation all 60 earlier rows were taken under.

`scripts/bench-ablations.sh` gained `--engine E` (passed straight through) and
`--label NAME`, which renames the `config` column and is **refused with more than one ref**,
since one name on two configurations is the same mislabelling in a different place.

V was then measured **through the orchestrator**, like every other configuration:

```
scripts/bench-ablations.sh --engine vm   --label V      main
scripts/bench-ablations.sh --engine tree --label V-tree main
```

#### The cache columns cross a boundary, and a control row prices it

Item 2.4 found that `argv[0]`'s **length** moves cachegrind's cache columns — 9.6% of
`bench/fib32.algo`'s D1 misses for 14 characters, binary held byte-identical. Measuring V
through the orchestrator puts its binary at `.worktrees/61996b03b61a/build-cfg/algo`, **38
characters, exactly H's**, so that half of the boundary is closed by construction rather
than by argument. What remains is that V's argv gains `--engine=vm` where H's had no flag
at all, and that `main`'s binary links four translation units H's did not.

**`V-tree` is the control for both.** It is the same worktree binary, invoked by the same
38-character path, running `--engine=tree`: everything V changes except the engine.

| Program | H (`perf/cum-d`) `Ir` | V-tree `Ir` | difference |
|---|---:|---:|---:|
| `bench/arith.algo` | 4,403,691,178 | 4,403,695,231 | **+4,053** |
| `bench/fib32.algo` | 8,432,437,963 | 8,432,442,016 | **+4,053** |
| `bench/loop10m.algo` | 3,921,639,753 | 3,921,643,806 | **+4,053** |
| `bench/vars.algo` | 2,149,802,840 | 2,149,806,857 | **+4,017** |

**A fixed offset of about four thousand instructions, identical on three programs of
different shapes and sizes** — process startup and one more argv element, not per-iteration
work. It says the tree-walker at `main` does byte-identical work to the tree-walker at H,
which is what Phase 4's "measured nothing, touched no interpreter source" claim asserts and
what nothing until now had checked against a counter. Four thousand in 2.1–8.4 **billion**
is 0.00005% to 0.0001%.

So: **the `Ir` column carries the H → V step** and is the column the attribution rests on,
as everywhere else here. The cache and branch columns are reported at V, but the comparison
in them that is fully controlled is **V against V-tree**, one row below it in each table —
same binary, same path, argv differing by two characters. H → V in a cache column crosses
both a binary and an argv boundary and must be quoted with that attached.

#### The lowered comparisons cost 57.00 instructions each, and this table contains none

Item 4.1 lowered `<=`, `>=` and `!=` onto `GT NOT`, `LT NOT` and `EQ NOT` — one extra
instruction per lowered comparison — and recorded that **5.1 must report its size**. Two
halves to the answer.

**None of the four benchmark programs contains a lowered comparison.** `grep -c '<=\|>=\|!='`
over `bench/*.algo` is 0 on all four: `arith`, `loop10m` and `vars` compare with `<` and
`fib32` with `<`. **So the cost is exactly zero in every cell of the four tables below**, and
V's losses there are not this.

Its size was measured separately, on a probe pair differing in one character-for-character
substitution and nothing else, under the **V worktree binary with `--engine=vm`**:

```
i = 0                       i = 0
c = 0                       c = 0
while i < 1000000 {         while i < 1000000 {
    if i < 1000000 {            if i <= 999999 {
        c = c + 1                       c = c + 1
    }                           }
    i = i + 1                   i = i + 1
}                           }
print c                     print c
```

Both print `1000000`, both take the true arm on all 1,000,000 iterations, and both loop
headers are the identical `i < 1000000`, so the two differ by exactly 1,000,000 executions
of `NOT` and nothing else. Under `valgrind --tool=cachegrind`:

| Probe | `Ir` |
|---|---:|
| `i < 1000000` | 1,239,672,638 |
| `i <= 999999` | 1,296,673,045 |
| difference over 1,000,000 lowered comparisons | **57,000,407 — 57.00 each** |

**57.00 instructions, to four significant figures of a division that had no reason to come
out round.** That is one turn of the VM's dispatch loop, and it agrees with the loop's cost
read off a table row independently: `bench/loop10m.algo` retires 6,071,652,034 `Ir` over
10,000,000 iterations of a body that compiles to ten instructions, i.e. about 60 per
instruction dispatched.

This is an **`Ir`-only figure taken outside the driver**, the instrument item 3.4
legitimised for exactly this and legitimate for exactly this — `Ir` is invocation-stable to
a few hundred counts in billions and the cache columns are not. The probes lived in
`build-probe/` and were deleted; they are quoted above in full so the number is
reproducible without them.

#### Where this table lives

**Here, in `results/README.md`, beside the Phase 3 workings the attribution is built from.**
`README.md` is Phase 6's artefact end to end — 6.1 gives it the table as *supporting*
material and 6.2 the headline — and 4.5 already deferred an excerpt to 6.1 on the same
reasoning. A table written into `README.md` under 5.1's tag would be a second change wearing
it. 5.1 assembles the table; 6.1 and 6.2 decide how much of it a reader meets first.

#### The tables

Δ is against the row above. Every cell is a committed row in `measurements.csv`,
`config` in {`v1-naive-treewalk`, `perf/cum-a`…`perf/cum-d`, `V`, `V-tree`}; the percentages
are arithmetic on those cells and nothing else.

##### `bench/arith.algo`
| Configuration | `Ir` | Δ `Ir` vs previous | `D1_misses` | `LL_misses` | `branches` | `mispredicts` | wall median (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| **N** — naive tree-walker | 15,081,690,621 | — | 20,609 | 13,115 | 2,936,317,621 | 108,015,316 | 849 |
| **+A** — pass by reference | 13,177,690,471 | -12.62% | 20,606 | 13,097 | 2,506,317,591 | 102,015,324 | 732 |
| **+B** — literals pre-parsed | 8,679,692,475 | -34.13% | 20,616 | 13,094 | 1,722,317,937 | 94,015,365 | 518 |
| **+C** — enum operator dispatch | 8,563,692,611 | -1.34% | 20,609 | 13,089 | 1,768,317,895 | 88,015,356 | 520 |
| **+D = H** — slot environment | 4,403,691,178 | -48.58% | 20,606 | 13,060 | 622,317,606 | 58,015,288 | 203 |
| **V** — bytecode VM | 8,379,713,777 | +90.29% | 20,745 | 13,243 | 1,132,320,246 | 192,016,013 | 486 |
| *(control)* **V-tree** — `main` on `--engine=tree` | 4,403,695,231 | — | 20,617 | 13,065 | 622,318,334 | 58,015,657 | 221 |

N → H **-70.80%** · H → V **+90.29%** · N → V **-44.44%** · output `24000000` at every configuration.

Control: V-tree − H = +4053 `Ir`.

##### `bench/fib32.algo`
| Configuration | `Ir` | Δ `Ir` vs previous | `D1_misses` | `LL_misses` | `branches` | `mispredicts` | wall median (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| **N** — naive tree-walker | 15,996,279,019 | — | 13,294,533 | 13,729 | 2,792,389,887 | 71,104,045 | 869 |
| **+A** — pass by reference | 14,611,120,098 | -8.66% | 10,413,266 | 13,655 | 2,496,325,387 | 37,802,024 | 810 |
| **+B** — literals pre-parsed | 12,781,864,895 | -12.52% | 8,709,033 | 13,650 | 2,186,162,655 | 40,767,634 | 672 |
| **+C** — enum operator dispatch | 12,732,520,893 | -0.39% | 8,709,030 | 13,652 | 2,210,834,680 | 49,331,163 | 658 |
| **+D = H** — slot environment | 8,432,437,963 | -33.77% | 11,870,385 | 13,494 | 1,273,560,628 | 41,664,747 | 431 |
| **V** — bytecode VM | 5,344,942,619 | -36.61% | 20,593 | 13,322 | 691,133,183 | 107,024,246 | 341 |
| *(control)* **V-tree** — `main` on `--engine=tree` | 8,432,442,016 | — | 11,901,112 | 13,509 | 1,273,561,356 | 31,397,591 | 413 |

N → H **-47.29%** · H → V **-36.61%** · N → V **-66.59%** · output `2178309` at every configuration.

Control: V-tree − H = +4053 `Ir`.

##### `bench/loop10m.algo`
| Configuration | `Ir` | Δ `Ir` vs previous | `D1_misses` | `LL_misses` | `branches` | `mispredicts` | wall median (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| **N** — naive tree-walker | 13,981,640,413 | — | 20,285 | 12,916 | 2,750,309,812 | 50,014,551 | 660 |
| **+A** — pass by reference | 12,611,640,285 | -9.80% | 20,280 | 12,902 | 2,450,309,787 | 30,014,551 | 603 |
| **+B** — literals pre-parsed | 8,891,640,413 | -29.50% | 20,280 | 12,894 | 1,730,309,809 | 10,014,570 | 434 |
| **+C** — enum operator dispatch | 8,821,640,456 | -0.79% | 20,279 | 12,892 | 1,750,309,803 | 20,014,557 | 431 |
| **+D = H** — slot environment | 3,921,639,753 | -55.55% | 20,272 | 12,868 | 480,309,660 | 20,014,528 | 175 |
| **V** — bytecode VM | 6,071,652,034 | +54.82% | 20,309 | 12,987 | 760,311,358 | 100,015,073 | 375 |
| *(control)* **V-tree** — `main` on `--engine=tree` | 3,921,643,806 | — | 20,283 | 12,879 | 480,310,388 | 20,014,912 | 169 |

N → H **-71.95%** · H → V **+54.82%** · N → V **-56.57%** · output `10000000` at every configuration.

Control: V-tree − H = +4053 `Ir`.

##### `bench/vars.algo`
| Configuration | `Ir` | Δ `Ir` vs previous | `D1_misses` | `LL_misses` | `branches` | `mispredicts` | wall median (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| **N** — naive tree-walker | 12,854,819,055 | — | 3,021,434 | 13,353 | 3,288,340,020 | 217,016,313 | 900 |
| **+A** — pass by reference | 11,949,818,523 | -7.04% | 21,427 | 13,336 | 3,093,339,900 | 214,016,277 | 837 |
| **+B** — literals pre-parsed | 11,970,818,501 | +0.18% | 21,423 | 13,337 | 3,093,339,900 | 214,016,373 | 831 |
| **+C** — enum operator dispatch | 11,963,818,820 | -0.06% | 21,425 | 13,325 | 3,143,339,878 | 214,016,272 | 829 |
| **+D = H** — slot environment | 2,149,802,840 | -82.03% | 21,371 | 13,275 | 316,336,539 | 5,015,899 | 95 |
| **V** — bytecode VM | 4,039,833,836 | +87.92% | 21,602 | 13,481 | 533,339,806 | 96,016,591 | 223 |
| *(control)* **V-tree** — `main` on `--engine=tree` | 2,149,806,857 | — | 21,390 | 13,280 | 316,337,263 | 5,016,304 | 95 |

N → H **-83.28%** · H → V **+87.92%** · N → V **-68.57%** · output `136000000` at every configuration.

Control: V-tree − H = +4017 `Ir`.

#### What the table says, both directions

**N → V is a 44–69% reduction in instructions retired. H → V is a LOSS on three programs
out of four**, and the second sentence is the one this project exists to publish:

| Program | N → H | H → V | N → V |
|---|---:|---:|---:|
| `bench/arith.algo` | −70.80% | **+90.29%** | −44.44% |
| `bench/fib32.algo` | −47.29% | **−36.61%** | −66.59% |
| `bench/loop10m.algo` | −71.95% | **+54.82%** | −56.57% |
| `bench/vars.algo` | −83.28% | **+87.92%** | −68.57% |

**The bytecode VM retires nearly twice the instructions of the hardened tree-walker on
`arith` and `vars`, and half again as many on `loop10m`.** It wins on `fib32` alone, by
36.61%, which is the one program of the four that calls a function 7,049,155 times: the VM
replaces `callFunction`'s recursion, argument vector and frame allocation with a push onto
a stack it already owns. Against N — the comparison a project that had skipped Phase 3
would have published — V looks like a 44–69% win everywhere. **Against the tree-walker with
four unforced inefficiencies removed, it is a win on one program in four.** That difference
is Phase 3's entire return.

The branch columns say the same thing from the other side. V's `mispredicts` are **3.3x**
H's on `arith` (192,016,013 against 58,015,288), **5.0x** on `loop10m`, **19.1x** on `vars`
and **2.6x** on `fib32` — the single indirect dispatch every bytecode VM funnels its whole
program through, against a tree-walker whose call sites are spread across the code and each
predictable on its own.

**And the loss comes with a win that is not in `Ir` at all.** V's `D1_misses` are ~20,600
on all four programs — including `bench/fib32.algo`, where H's are **11,870,385** and
V-tree's 11,901,112. **The VM removes 99.83% of that program's D1 misses**, because a frame
is a slot range on a stack it already touched rather than a heap block per call. V vs V-tree
is the controlled form of that comparison (same binary, same 38-character path); H → V
crosses the argv and binary boundary the control row above prices, and the direction and
the order of magnitude survive it either way.

Two further things the tables record and 5.1 does not explain:

- **The cumulative `+B` step on `bench/vars.algo` is positive** (+20,999,978), where B in
  isolation moved that program by 23 instructions in 12.85 billion. The A×B interaction is
  recorded in the item-3.2 section above as exactly one instruction per `IdentifierNode`
  evaluation. **Item 5.2 owns the residual**; this row is one of its inputs, not an
  explanation of it.
- **Wall clock at V is 223–486 ms**, all four below the 0.5–5 s acceptance band's floor, as
  H's already were. Nothing is invalidated and **no `n` was raised**: the band was set on
  the baseline, wall clock is narrative only, and cachegrind's counts are deterministic at
  any size.
