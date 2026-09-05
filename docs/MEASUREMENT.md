# Algo — the measurement platform

> **Scope of this file.** Roadmap item **2.1** requires the image digest, the CPU
> model and the compiler version to be written down, because every published
> number is meaningless without them. That is what is here, together with the
> decisions 2.1 had to settle before any number could be taken at all.
> The **Boundary of the claim** section at the end was item **5.3**'s to fill
> in, and it is filled in. It was stubbed from the start so that limitations had
> somewhere to land as they were found, rather than being bolted on after the
> numbers existed.

Every number in this repository is produced inside the container described
below. Nothing is measured on the host.

## Why a container is not optional

Development happens on Apple Silicon. Valgrind does not meaningfully support
arm64 macOS, so `cachegrind` — the instrument the committed numbers come from —
cannot run on the host at all. This is not a preference for tidiness or a way of
pinning a toolchain. Outside this container there is no measurement.

## The image

```
ubuntu@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517
```

**Pinned by digest, not by tag.** `ubuntu:24.04` is rebuilt whenever one of its
base packages changes, so the tag moves; a count measured against a moving base
is not reproducible, and the difference would show up months later as an
unexplained step in a series that is supposed to isolate one change at a time.

The digest above is the multi-arch **OCI index** digest, which is the right
thing to pin: one `FROM` line names exact content while still resolving to the
arm64 manifest here and the amd64 one on a CI runner. The per-architecture
manifests it resolves to:

| Platform | Manifest digest |
|---|---|
| linux/arm64/v8 *(this machine)* | `sha256:95fa486768020359141f1318720f43e7982ef926c792891d984aef9aaf05e7ea` |
| linux/amd64 | `sha256:1e0a86e57d247923571b75e0aaf48a1449cf8c543d51fb3e07a4a7d7bfa79316` |

Obtained by pulling and reading back from the daemon, not copied from a registry
page:

```bash
docker pull ubuntu:24.04 && docker image inspect ubuntu:24.04 --format '{{json .RepoDigests}}'
```

**The pin stops at the image.** `apt-get install` resolves package versions when
`Dockerfile.bench` is *built*, not when it is run, so the Ubuntu archive can
move underneath a rebuild even though the base layer cannot. The reproducibility
boundary is therefore the built image, and the versions that were actually
installed are recorded below so a future rebuild that differs is visible rather
than silent.

## Recorded platform

Read out of the running container, except the host CPU row, which the guest
cannot see.

| | |
|---|---|
| Image digest | `sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517` |
| Guest architecture | `aarch64`, 8 CPUs |
| Guest CPU model | **not reported** — `lscpu` gives `Vendor ID: Apple`, `Model name: -`; `/proc/cpuinfo` gives `CPU implementer 0x61`, `CPU part 0x000` |
| Host CPU | `Apple M2` (`Mac14,7`), read on the host with `sysctl -n machdep.cpu.brand_string` |
| Compiler | `c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0` |
| Valgrind | `valgrind-3.22.0` |
| Package versions | `valgrind 1:3.22.0-0ubuntu3` · `cmake 3.28.3-1build7` · `gcc`/`g++ 4:13.2.0-7ubuntu1` · `build-essential 12.10ubuntu1` · `linux-tools-generic 6.8.0-138.138` |
| Guest kernel | `6.12.68` (Docker Desktop LinuxKit) |
| Build type | `RelWithDebInfo` (`-O2 -g -DNDEBUG`) |
| `ALGO_SANITIZE` | `OFF` — echoed by every run, not assumed |

The masked CPU part is worth noticing rather than glossing over: **the container
cannot tell which chip it is running on.** That costs nothing here, because the
committed instrument simulates a machine instead of measuring this one — but it
is a second, independent reason no hardware-counter number from this platform
could ever be traceable.

## Native arm64, not emulated x86_64

The container runs **native arm64**. `compose.yaml` sets no `platform:` key.

This was tested rather than assumed, because it is the question item 2.1 is
really asking. Cachegrind is fully supported on arm64 Linux: it runs, it accepts
`--cache-sim=yes --branch-sim=yes`, and it reports every counter item 2.3 needs.

x86_64 under emulation also works on this machine — Docker Desktop's Rosetta
handles it — so the choice is real. Native still wins, on three grounds:

1. **Emulation would sit underneath the thing being measured.** The project's
   claim is an attribution of a result to named causes in the interpreter. A
   binary-translation layer between the binary and the simulator is one more
   uncontrolled cause, and no ablation accounts for it.
2. **Emulation makes the counts *less* portable, not more.** Under emulation
   valgrind auto-detects a host-derived cache model, whereas natively it uses
   its own fixed defaults. This is not a small effect — see the table below.
3. It is slower: the same cachegrind run takes **1.30x** as long
   (7,758 ms emulated against 5,951 ms native, binary already built in both).

Both arrangements were built and run, same commit, same workload:

| | native arm64 | emulated x86_64 |
|---|---|---|
| Cache model | valgrind defaults | **auto-detected from the host** |
| I1 / D1 | 16 KB, 4-way | 128 KB, 8-way |
| LL | 256 KB, 8-way | 8 MB, 16-way |
| I refs | 1,443,377,234 | 1,397,027,392 |
| **D1 misses** | **1,340,872** | **12,257** |
| cachegrind wall clock | 5,951 ms | 7,758 ms |

The instruction counts sit within 3.3% of each other — same work, two
instruction sets. The **D1 miss counts differ by 109x**, and nothing about the
program changed: emulation let valgrind read the M2's real cache sizes through
Rosetta's emulated `CPUID`, and an 8 MB last-level cache simply does not miss
where a 256 KB one does.

That number is the argument for pinning the model rather than detecting it. A
cache figure that swings by two orders of magnitude depending on what the
simulator believes about the host is a figure that must be reported as a
property of a model — which is exactly how *Boundary of the claim* reports it.

Note that the two architectures are not comparable to each other and are not
meant to be: they are different instruction sets, so the same program retires a
different number of instructions on each. Nothing in this project ever compares
a count taken on one to a count taken on the other.

## The simulated cache model

Every native run prints:

```
Warning: Cannot auto-detect cache config, using defaults.
```

This is **not a defect, and it must not be "fixed."** On arm64 valgrind cannot
read the CPU's cache descriptors, so it falls back to a built-in cache model.
Run with `-v`, it states the model it used:

```
Cache configuration used:
  I1: 16,384 B, 4-way, 64 B lines
  D1: 16,384 B, 4-way, 64 B lines
  LL: 262,144 B, 8-way, 64 B lines
```

That is a small, generic machine, and it is emphatically **not an Apple M2**:
the emulated run, which *can* see the host, reports 128 KB L1 and an 8 MB
last-level cache — eight and thirty-two times larger. The consequence is the
useful one: the simulated counts depend on valgrind's version and on nothing
about this laptop, so a count taken here reproduces on any arm64 machine running
the same image.

Under x86_64 emulation the warning **disappears**, because Rosetta emulates
`CPUID` and valgrind successfully auto-detects a cache configuration — one
derived from the host. That is the less reproducible arrangement of the two, and
the 109x swing in D1 misses in the previous section is how much it matters.

What this buys and what it costs is stated plainly in *Boundary of the claim*:
the simulated hierarchy is not this machine's hierarchy, and the cache and
branch figures are properties of a model rather than of silicon. The instruction
count, which is what the attribution actually rests on, does not depend on the
cache model at all.

## Optimisation level — a decision, not an inheritance

**Benchmarks are built `RelWithDebInfo`: `-O2 -g -DNDEBUG`.**

Nothing in `CMakeLists.txt` sets `CMAKE_BUILD_TYPE`, so the default build — the
one `ctest` uses — is unoptimised. Had that been left to carry through into
Phase 2, it would have silently decided every number in the project. It is set
explicitly in `Dockerfile.bench` instead.

The level was chosen against measurement, not argument. The same program, the
same container, the same commit:

| | `-O0` (unset, the default) | `-O2` (`RelWithDebInfo`) | ratio |
|---|---|---|---|
| I refs | 9,047,985,149 | 1,443,377,234 | **6.26x** |
| D refs | 4,915,612,422 | 681,861,801 | 7.20x |
| Branches | 456,648,244 | 251,971,002 | 1.81x |
| Wall clock, best of 3 | 752 ms | 80 ms | 9.40x |

At `-O0` the interpreter is charged **six times** the instructions a real build
executes. Every ablation delta measured there would be inflated by whatever
optimisation the compiler declined to attempt, and a speedup claim resting on it
would be measuring the compiler's idleness rather than the design — which is the
standard way a benchmark result gets dismissed, and rightly.

The obvious objection cuts the other way. One might keep `-O0` so the optimiser
cannot erase the very inefficiency an ablation is trying to price. But if `-O2`
can erase an inefficiency, then its true cost *is* smaller, and reporting the
`-O0` figure would overstate it. The honest number is the one a real build pays.

Two smaller choices inside that:

- **`-O2`, not `-O3`.** `-O3`'s extra unrolling and vectorisation do little for
  an interpreter dispatch loop and make counts more sensitive to the compiler
  version, which is a reproducibility cost for no analytical gain.
- **`-g` is kept.** It does not change generated code, and attributing
  instructions to source lines needs symbols. Item 2.2 has since done exactly
  that and confirmed the flag earns its place — but **not** via `cg_annotate`,
  which does not run in this image; see below.

Three consequences to keep in view:

1. **The measured binary is not the tested binary.** `ctest` on the host builds
   unoptimised with AppleClang; the bench build is GCC 13.3.0 at `-O2`. So the
   optimised configuration is tested too, by `ctest --test-dir build-bench`
   inside the container. **The `32/32` recorded here was the suite as it stood
   then** — item 4.4 later registered every golden case against both engines and
   4.5 added an eighth unit binary, so the suite now holds **66** tests. The
   in-container run has not been re-taken since, so its current result is
   `<pending>`; re-run it and write down what it prints.
2. **The bench build gets a directory of its own**, `build-bench/`. The
   repository is bind-mounted, so building into `build/` would overwrite the
   host's AppleClang objects with Linux GCC ones and leave neither tree usable.
   `.gitignore`'s existing `build-*/` already covers it.
3. **`ALGO_SANITIZE` stays `OFF`.** It is never passed, and each run echoes the
   cache value rather than trusting that. A sanitizer here would put its own
   instrumentation into every number Phase 3 attributes to a cause.

## The two instruments

| | `cachegrind` | `perf` |
|---|---|---|
| Kind | simulated | hardware counters |
| Deterministic | yes | no |
| Machine-specific | no | yes |
| Committed as thresholds | **yes** | **never** |
| Available here | yes | **no** |

`perf` **does not work on this platform**, and this is recorded rather than
worked around. `linux-tools-generic` is installed as the roadmap asks, but the
tools are built for kernel 6.8 while Docker Desktop's virtual machine runs
LinuxKit 6.12.68:

```
WARNING: perf not found for kernel 6.12.68
```

No `linux-tools-6.12.68-linuxkit` package exists, and a guest would not be given
`perf_event` access even if it did. Item 2.3 already treats `perf` as optional
for exactly this reason, so this blocks nothing: it removes the wall-clock
narrative's hardware counters, not the attribution. Every number the attribution
rests on is simulated and therefore still available.

### `cg_annotate` does not run here either — read the out-file directly

Valgrind 3.22 ships `cg_annotate` as a Python script and this image installs no
`python3`, so it exits immediately:

```
/usr/bin/env: 'python3': No such file or directory
```

**This costs nothing, because the tool is a presenter rather than an
instrument.** Cachegrind writes everything it knows to the path given by
`--cachegrind-out-file`, in a documented plain-text format: a header naming the
event order, then `fl=` / `fn=` records and one line of counts per source line.

```
events: Ir I1mr ILmr Dr D1mr DLmr Dw D1mw DLmw Bc Bcm Bi Bim
```

Item 2.2 needed per-function and per-line attribution to establish what
`bench/vars.algo` actually spends its instructions on, and got it by parsing
that file — which is also what confirms `-g` is pulling its weight, since the
`fl=` and `fn=` records come from the debug information. Note the default
command passes `--cachegrind-out-file=/dev/null`, so any run wanting attribution
must give it a real path.

**Adding `python3` to `Dockerfile.bench` was deliberately not done.** The image
is item 2.1's deliverable, and changing it inside a later item would mix two
items in one commit — the exact coupling the one-item-one-commit rule exists to
prevent, and the thing that makes a configuration non-isolable by tag. If a
Phase 3 item wants `cg_annotate` routinely, it should add it as its own change
and re-record the package versions in the table above.

## Running it

```bash
docker compose run --rm bench
```

That configures `build-bench/`, builds it, echoes the platform, and runs
cachegrind over `examples/fib.algo`. It is deliberately inline in
`Dockerfile.bench`, and it remains item 2.1's acceptance criterion — a
self-contained check that the platform works at all.

**Item 2.3's `scripts/bench.sh` has since superseded it for measurement.** The
inline command prints a summary and keeps nothing; the driver writes a row:

```bash
docker compose run --rm bench bash scripts/bench.sh build-bench/algo bench/fib32.algo --config baseline
```

It refuses to run off Linux and refuses a build whose `CMAKE_BUILD_TYPE` is
unoptimised, and it records the platform in every row so a mismatch with the
table above is visible in the data. `results/README.md` documents the schema and
which columns may be committed as thresholds. **Every number in this repository
traces to a row in `results/measurements.csv`.**

Two facts about this image, established by item 2.3 and load-bearing for 2.4:
**`git` is not installed** — so `git worktree add` cannot run inside the
container, and the driver reads `.git` directly rather than shelling out — and
**neither is `jq`**, which together with the absent `python3` is why the result
ledger is CSV parsed with `awk`.

### Measuring several configurations — `scripts/bench-ablations.sh`

Item 2.4's orchestrator. It takes a list of refs and, for each, creates a
detached git worktree, builds it into a directory of its own and hands it to
`scripts/bench.sh` once per benchmark program:

```bash
scripts/bench-ablations.sh v1-naive-treewalk perf/iso-a perf/iso-b
scripts/bench-ablations.sh --dry-run main 75b84e8      # prints the plan, measures nothing
```

**It runs on the host and `scripts/bench.sh` runs in the container, and that
split is forced rather than chosen.** `git` is not in the bench image, so
worktree creation is host-only; valgrind does not work on arm64 macOS, so
measurement is container-only. Something has to cross the boundary, and the
orchestrator is the only correct place for it — a driver that re-invokes docker
could not itself be called from inside docker, and translating a host path into
a container path across a bind mount covering the repository only is the
silently-wrong-binary failure the driver's refusals exist to prevent. The two
halves therefore refuse in opposite directions: `bench.sh` refuses off Linux
(exit 69), `bench-ablations.sh` refuses inside the container (exit 69).

The worktrees live under `.worktrees/` **inside the repository**, because compose
bind-mounts the repository as `/repo` and a worktree anywhere else is invisible
to the container. A worktree's `.git` file names a host path that means nothing
inside the container; that does not matter, because building needs cmake, not
git. What does matter is that the container therefore cannot resolve the commit,
so the orchestrator resolves each ref on the host and passes the full SHA
through `bench.sh --commit`.

Three things are held fixed while the interpreter varies: the **driver** and the
**programs** are always this checkout's, never the worktree's — the instrument
and the stimulus are not part of the hypothesis, and a ref older than item 2.2
has neither — and the **invocation**, for the reason in the next section.

The Docker daemon must be running — on this machine it is not started at login,
and `docker info` failing with a missing socket means `open -a Docker` and about
seventy seconds, not that anything is broken.

## Reproducibility, as actually observed

Item 2.1's acceptance criterion is that a second run reproduces the instruction
count exactly. **Four** consecutive runs were compared field by field — three via
`docker compose run --rm bench` and a fourth via the roadmap's exact
`docker compose run bench`. **All four are identical** — not close, identical:

```
I refs:        1,443,377,234
I1  misses:        1,274,297      LLi misses:            2,473
D refs:          681,861,801  (439,024,114 rd + 242,837,687 wr)
D1  misses:        1,340,872      LLd misses:           11,146
LL refs:           2,615,169      LL misses:            13,619
Branches:        251,971,002  (234,806,951 cond + 17,164,051 ind)
Mispredicts:       6,406,933  (  6,406,436 cond +        497 ind)
```

`examples/fib.algo` prints `196418` on every run, which is `fib(27)` and Phase
1's acceptance criterion.

Runs 2 to 4 reused the binary run 1 built, which is the honest reading of the
criterion: what is being tested is that the *instrument* is deterministic, and
it is.

Prefer the `--rm` form in day-to-day use. Without it each invocation leaves a
stopped container behind; they are harmless but they accumulate.

## Boundary of the claim

> **Item 5.3 closes this section**, and closing it is the item's whole content.
> Most of what follows was not written by 5.3: each entry was left by the item
> that ran into it, at the time it ran into it, so that a limitation is recorded
> by the session that has the evidence for it rather than reconstructed later by
> one that does not. 5.3 audited what was here, added the four entries no item
> had yet been in a position to write — the scope of the whole result, the
> locality that ablation E was cut before it could separate, the boundary the
> H → V cache columns cross, and the cycle-level column that does not exist on
> this platform — and stopped.

**A limitation stated before it is found is a credential; the same limitation
found by a reader is a wound.** Nothing below is softened, and nothing below is
a result wearing a caveat's clothes. The question each entry answers is *what
does the attribution not establish*.

- **The simulated cache hierarchy is not this machine's hierarchy** — and on
  arm64 it does not even attempt to be, as the *simulated cache model* section
  above describes. The cache and branch-prediction figures are properties of
  valgrind's default model. The instruction counts do not depend on it.
- **The overflow check does not cancel exactly in wall-clock time.** Left by
  item 1.5. Every configuration pays for the arithmetic overflow check,
  including the VM, and no ablation removes it — which is what makes it cancel
  in the deltas. It cancels *exactly* only for instruction counts, where it is
  one extra test per arithmetic operation, present everywhere. For wall clock it
  does not, because that branch competes for the same execution resources the
  interaction residual is itself about. The reasoning is in the 1.5 entry of the
  roadmap's *Decisions and deviations* and above `binaryOverflowFault` in
  `src/interpreter.cpp`.
- **The cache counts depend on how the process was launched; the instruction
  count does not.** Left by item 2.3, found while validating the driver. Same
  binary, same commit, same container, `bench/vars.algo`: launched directly from
  the shell it reports **10,021,412** D1 misses; launched through an intervening
  `env`, or through `scripts/bench.sh`, it reports **6,021,415–6,021,416**. `Ir`
  across those same runs spans **448 counts** — 12,854,818,629 at the low end and
  12,854,819,077 at the high, or 0.0000035% of the total, which is process
  startup and nothing else. The two D1 values differ by exactly
  **4,000,000**, four per iteration of a 1,000,000-iteration loop.

  The discriminator is the **environment block**, which shifts the initial
  process layout: adding a whole variable flips the count, adding one or two
  bytes to an existing variable does not, and `argv[0]`'s length does not matter.
  Each invocation style then reproduces exactly — the instrument is
  deterministic, as the section above records; what varies is the process it is
  handed.

  Two consequences. **The cache and branch figures are comparable only across
  rows produced by the same driver**, which is the strongest single argument for
  `scripts/bench.sh` existing: it fixes the invocation. A count taken by hand at
  a shell prompt measures something real but must never be quoted beside a
  committed row. And **the attribution rests on `Ir`, which does not move** —
  this is a second, independent reason the cache figures are reported as
  properties of a model rather than facts about silicon, the first being
  valgrind's default cache hierarchy.

- **Ablation D's figure will be a property of a 20-name frame, not of the
  language.** Left by item 2.2. What ablation D removes — a `std::map` keyed on
  `std::string`, replaced by a slot-indexed vector — costs more the deeper the
  map is and the more expensive its key comparison, which is to say it scales
  with *how many variables the measured program declares*. `bench/vars.algo` is
  the program that will attribute D, and it declares 20. That number is a design
  choice, not a property of Algo, and a program declaring two would report a
  much smaller D while measuring the same interpreter.

  The size of the effect is already visible. Against a control identical in node
  count, operator mix and literal count but holding **5** names rather than 20,
  the larger frame alone accounts for **2,882,108,629 instructions — 22.4% of
  `bench/vars.algo`'s mix — and 99.8% of its D1 misses** (10,021,413 against
  20,688). **Those D1 figures predate `scripts/bench.sh` and were taken by hand,
  so they are not comparable to the committed rows**, which report 6,021,416 for
  the same binary and program — see the invocation-sensitivity entry below. The
  instruction figure is unaffected, and the ratios above used one methodology
  throughout, so they stand as stated. Map machinery plus string-key comparison is somewhere between **23%
  and 56%** of its instructions; the spread is only how much of `bcmp` serves map
  keys rather than the operator-dispatch chain, and ablation D is itself the
  instrument that resolves it.

  So D's number is reportable as measured, but **must be reported as "on a
  20-name frame"** rather than as what the environment costs in general. The
  names are also short (`v01`…`v16`), which understates the effect rather than
  inflating it, since longer keys cost more per comparison.

  **Item 3.1 has since narrowed that pre-attribution, and only its cache half.**
  The instruction figure stands. The D1 figure does not transfer: ablation A —
  which touches nothing about the environment — removed **99.29%** of
  `bench/vars.algo`'s D1 misses, 3,021,434 down to 21,427, both measured through
  the driver at the same path length. So most of the miss traffic the 5-name
  control attributed to frame depth was the reference-count write landing in
  each node's control block *while* the map walk was in flight, not the map walk
  itself. The two causes overlap, which is what the interaction residual is for
  — but it means **ablation D should not be expected to show a large D1 effect
  once A is already applied**, and if a session finds itself surprised by a small
  one, this is why. `Ir` is unaffected by any of this.

  **Item 3.4 has now measured all of it, and every part of the paragraph above
  held.** The 20-name caveat stands and must still be attached to the figure:
  D removes **76.33%** of `bench/vars.algo`'s instructions applied to N alone and
  **82.03%** applied on top of A, B and C, against **35.12%** on the one-name
  frame of `bench/loop10m.algo` and **27.23%** on `bench/fib32.algo`'s one
  parameter per call. The ratio between the extremes is the whole content of the
  caveat, and a program declaring two names would report something near arith's
  −27.56%.

  **The 23%–56% bracket resolves above its own top end, and the reason is a
  boundary of the method rather than of the ablation.** That bracket was built by
  adding up cachegrind's per-function totals for the functions whose names are
  the map. But `std::map::find` is **inlined into `Interpreter::evaluate`**, so
  its descent loop is charged to `evaluate` and never entered the bracket at all;
  only the out-of-line `map::operator[]` and `bcmp` did. Attribution by function
  name has the same failure mode as attribution by source text, which is item
  3.3's finding arriving from a second direction. **A bracket built from a
  profile is a lower bound whenever the thing being priced can be inlined.**

  **The D1 half is confirmed exactly, and the overlap is total rather than
  partial.** Isolated, D removes 99.29% of `bench/vars.algo`'s D1 misses —
  3,021,434 down to 21,378, the same three million ablation A removes. Applied
  on top of A, which has already taken them, D moves **fifty-four misses,
  −0.25%**. Two independent causes, one set of misses; the measured allocation
  arithmetic is that a twenty-name frame is twenty separate 88-byte blocks as a
  `std::map` and one contiguous 328-byte block as a `std::vector`.

- **The length of the binary's own path moves the cache columns too, and by more
  than the environment block does.** Left by item 2.4, and it sharpens the entry
  above rather than repeating it. Item 2.3 found that the environment block
  shifts the initial process layout while `argv[0]`'s length appeared not to; it
  does, and the effect is larger than anything 2.3 saw.

  The same binary *image* — verified identical: same `text`/`data`/`bss` sizes
  and byte-identical `LOAD` program headers, differing only in debug-info bytes
  that are never mapped — running `bench/fib32.algo` in the same container:

  | Invoked as | length | `Ir` | `D1_misses` |
  |---|---|---|---|
  | `build-bench/algo` | 16 | 15,996,278,997 | **15,000,480** |
  | `.worktrees/main/build-cfg/algo` | 30 | 15,996,278,991 | **13,560,023** |
  | `build-bench/xlgo` — the *worktree's own binary*, copied to a 16-character path | 16 | 15,996,278,997 | **15,000,480** |

  The third row is the control that makes this a mechanism rather than a
  coincidence: it holds the binary image fixed and changes only the path it is
  invoked by, and it reproduces the first row **exactly on all seventeen
  cachegrind columns**. `Ir` moves by 6 counts in 16 billion.

  Two consequences, and the first is structural.

  **`scripts/bench-ablations.sh` names each worktree after its commit rather
  than after the ref that asked for it**, so that
  `.worktrees/<12 hex>/build-cfg/algo` is exactly 38 characters for every
  configuration. Naming them after tags would have made `perf/iso-a` and
  `perf/cum-a` differ in path length and therefore in `D1_misses` — an ablation
  delta manufactured entirely by a tag name, in a series whose entire purpose is
  attributing deltas to causes. The script refuses to run a series whose paths
  are not all the same length.

  **Rows measured from a worktree are not comparable, in their cache columns, to
  the eight `baseline` rows item 2.3 took as `build-bench/algo`** — 38 characters
  against 16. So Phase 3 must measure configuration N *through the orchestrator*
  like every other configuration, rather than reusing those rows as N. `Ir` is
  comparable across both, and the attribution rests on `Ir`.

- **Ablation A did not remove atomics, and its number must not be read as the
  price of atomic reference counting.** Left by item 3.1, and checked in the
  binary rather than assumed, because the roadmap predicts for A "a
  disproportionate effect on atomics-heavy workloads" and that mechanism is not
  the one operating here.

  libstdc++ selects `_S_atomic` for `shared_ptr` at compile time, but the
  operation it emits then dispatches at **run time** on glibc's
  `__libc_single_threaded` (glibc 2.32 and later; this image has 2.39). The
  measured binary imports that symbol, and disassembling `Interpreter::evaluate`
  in configuration N shows the shape plainly — the flag is loaded, tested, and
  the plain path taken:

  ```
  ldr   x23, [x23, #4048]   ; &__libc_single_threaded, via the GOT
  ldrb  w1, [x23]
  cbz   w1, <atomic path>   ; not taken: algo never creates a thread
  ldr   w2, [x22, #8]       ; the increment, plain
  add   w2, w2, #0x1
  str   w2, [x22, #8]
  ```

  The counted object really is the atomic instantiation — cachegrind demangles
  the destroyed nodes as
  `std::_Sp_counted_ptr_inplace<BinOpNode, …, (__gnu_cxx::_Lock_policy)2>`, and
  policy 2 is `_S_atomic`. The dispatch is what makes the difference, and it
  resolves at run time.

  **Confirmed by execution, not only by reading the disassembly.** Every atomic
  read-modify-write in this binary goes through one of libgcc's outline helpers,
  which cachegrind lists by name whenever it retires an instruction. Run against
  a small `while` program with a real `--cachegrind-out-file`, the out-file names
  717 functions — `Interpreter::evaluate` among them — and
  **`__aarch64_ldadd4_acq_rel`, the helper the reference count would use, is not
  one of them.** It never executes. Two *other* outline helpers do,
  `__aarch64_cas4_acq` and `__aarch64_swp4_rel`, and they are the honest control
  on the method: both run inside `init_have_lse_atomics` and `pthread_once` while
  the loader starts the process, nowhere near the walk. `algo` never creates a
  thread, so **the count was maintained non-atomically on every node visit.**

  What A priced is therefore ordinary integer traffic — per `evaluate` call, a
  GOT load, two flag loads, a load/add/store, a load/sub/store, an acquire load
  of the fused use-and-weak word, and five conditional branches. The branch
  counter confirms the shape independently: the branch delta divided by the
  `evaluate` calls each program's source implies is **exactly 5.0** on all three
  iterative benchmarks.

  Two consequences for how A's figure may be quoted. It is a **lower bound** on
  what the same change is worth where the dispatch resolves the other way — a
  multi-threaded host program, an older glibc, or a libstdc++ built without the
  single-threaded fast path — and there the removed work would be genuine
  acquire-release atomics, whose cost in *cycles* is far above their cost in
  instructions. And it is a reminder that cachegrind counts instructions
  retired: an atomic increment and a plain one are close to equal in `Ir` and
  are not close to equal in time. The wall-clock columns are narrative only and
  cannot settle the difference either.

- **Ablation B makes configuration B accept fewer programs than configuration
  N, so the series is not strictly measuring one fixed language.** Left by item
  3.2. Every other ablation so far is semantics-preserving; this one is not,
  and the difference is worth stating rather than discovering later.

  Before B, an integer literal too wide for the value type was detected by
  `std::stoll` during the walk. Moving the conversion to parse time — which is
  what B *is* — moves the detection with it. It stays a `CompileError` and
  therefore exit code 65, with the same message and the same caret, all three
  pinned by `tests/diagnostic_test.cpp`. What changes is reachability: the check
  used to fire only on literals the program actually evaluated, and now fires on
  every literal in the file. A program with an out-of-range literal inside a
  function nobody calls ran clean under N and is rejected under B.
  `tests/error_overflow_unreached.algo` pins that, and nothing in the suite
  covered it before — reverting `src/` to the pre-B tree leaves all thirty-two
  existing cases green.

  **Why this does not contaminate the attribution.** The change is a strict
  widening: B rejects a superset of the programs N rejects, and for every
  program both accept the output is identical. None of the four benchmark
  programs contains an out-of-range literal anywhere, reached or unreached, so
  no measured row is affected. The alternative — keeping the detection at
  evaluation time by storing a validity flag and testing it on every visit —
  would have put a branch back on the hot path and made B remove something other
  than the re-parse, which is a worse contamination than the one being avoided.

  **How to quote B's figure.** As the cost of re-parsing literals during
  evaluation, in a language whose out-of-range check is a compile-time check
  either way. Not as a free lunch: a language that genuinely needed the check
  deferred to first execution would have to keep something on the hot path, and
  B's number would be smaller.

- **An ablation's measured delta includes the compiler's response to it, and at
  A and B that response is larger than it looks.** Left by item 3.2, and it
  bears directly on how item 5.2's residual should be explained.

  A and B remove disjoint work, and the branch counter says so exactly: the
  interaction `(N−A) + (N−B) − (N−AB)` in the `branches` column is **zero on all
  four benchmark programs**, not approximately zero. In `Ir` the same
  interaction is positive and is **exactly one instruction per `IdentifierNode`
  evaluation** — 14,000,000 against 14,000,001 implied on `arith`, 17,622,885
  against 17,622,887 on `fib32`, 20,000,000 against 20,000,001 on `loop10m`,
  21,000,001 against 21,000,002 on `vars`.

  Neither ablation touches the identifier arm. With no branch interaction at
  all, the mechanism cannot be control flow; it is code generation.
  `Interpreter::evaluate` is a materially different function in each of the four
  configurations — `0xfd4` bytes in N, `0xc44` in A, `0xd70` in B, `0x938` in
  A+B, read with `nm -S` from the kept worktree binaries — and removing one
  arm's work changes what the compiler does with the rest.

  **The consequence for the residual is that it will not decompose into
  "overlapping stalls" alone.** The roadmap's stated mechanism for a positive
  residual is memory stalls hiding each other's cost, which is a claim about
  *cycles*. This component is visible in *instructions retired*, where no stall
  can hide anything, so it is a second and independent source of the same sign.
  Item 5.2 should report both rather than attributing the whole residual to
  overlap.

- **An ablation that changes a node's size moves the cache columns, and the
  effect is not attributable to what the ablation claims to remove.** Left by
  item 3.2, and it is the third mechanism on this list that moves `D1_misses`
  without moving `Ir` — after the environment block and the length of
  `argv[0]`.

  B adds a `std::int64_t` to `NumberNode` and keeps the digits, taking the node
  from **48 bytes to 56** under the toolchain that produces these rows (GCC
  13.3.0, libstdc++; measured with a `sizeof` probe compiled against each
  worktree's `src/`). `bench/fib32.algo` — by a wide margin the heaviest
  allocator and walker of nodes among the four programs — moves **−15.57% on
  `D1_misses`** under B, while `arith`, `loop10m` and `vars` move by single
  counts. Every literal in `fib32` is a single digit, so nothing about the
  literal path explains a cache effect there.

  **There was no size-preserving option.** Dropping `text` instead of adding
  beside it would have taken the node to 24 bytes — a −24 change against this +8
  one — so the choice was between two perturbations and the smaller was taken.
  Ablation E, the item that would have priced node layout, was cut on
  2026-08-30, which means **no row in this repository attributes anything to node
  size**. That is what makes the effect a boundary rather than a result.

  **How to read a cache column in Phase 3.** Before attributing a `D1_misses`
  move to an ablation's named cause, check whether that ablation changed the
  size of a node. `Ir` does not move for this reason and the attribution rests on
  `Ir` — which is now true for four independent reasons on this list.

- **Ablation B changed the language, so the series is not measuring one fixed
  set of accepted programs.** Left by item 3.2. Full argument is in the entry
  above on B's reachability change; it is repeated in the list here only because
  a reader scanning for *what limits the claim* should not have to find it
  inside a paragraph about exit codes. **B is the only ablation that does this**
  — A, C and D are semantics-preserving — and no benchmark program is affected,
  so no committed row moves because of it.

- **Ablation C's figure does not price a chain of string comparisons, because
  the compiler never emitted one — and C makes the branch count go up.** Left by
  item 3.2's successor, item 3.3, and read out of the binary rather than
  inferred, in the same way item 3.1's atomics entry above was. The roadmap's
  description of C is *"operator selection is a chain of `node->value == "+"`
  string comparisons"*, and at source level that is exactly what configuration N
  contains. It is not what runs.

  Disassembling `Interpreter::evaluate` in N (GCC 13.3.0, `-O2`, aarch64) shows
  GCC had already collapsed the ten comparisons into a **length dispatch and a
  one-byte compare chain**: `op.size()` is loaded once and tested against 1 and
  2, and the six one-character operators are then separated by a single `ldrb`
  and a run of `cmp`/`b.eq` against `'+'`, `'-'`, `'*'`, `'/'`, `'<'`, `'>'`.
  There is no `memcmp` call, no per-candidate length, and no string comparison
  in the machine sense at all.

  ```
  ldr   x2, [x20, #24]      ; op.size()
  cmp   x2, #0x1
  b.eq  <one-character path>
  ...
  ldr   x0, [x20, #16]      ; op.data()
  ldrb  w0, [x0]
  cmp   w0, #0x2b           ; '+'
  b.eq  <Add>
  cmp   w0, #0x2d           ; '-'
  b.eq  <Subtract>
  ```

  **What replaced it is a binary decision tree, not a jump table.** GCC compiled
  the ten-enumerator `switch` into nested `cmp`/`b.eq`/`b.gt`, and the
  indirect-branch column says so independently: `Bi` moves by at most 20 counts
  anywhere in the series, so no indirect jump was introduced.

  **Three consequences for how C's number may be quoted.**

  (i) The instruction win is real but small — between 0.05% and 0.76% of `Ir` on
  configuration N, and 0.06% to 1.34% on top of A and B — and it is small
  *because the compiler had already done most of the work the ablation claims to
  do*. C is by a wide margin the cheapest of the four unforced inefficiencies,
  and any narrative that prices it from the source text will overstate it.

  (ii) **The branch column moves the other way.** C adds 20 to 50 million
  conditional branches per program — 0.73% to 2.67% — because the character
  chain finds `+` on its first comparison in two branches where the decision tree
  needs five to reach `Add`. Only `*`, `/` and `<` come out ahead. A program
  whose only binary operator is `+` gains nothing in instructions and pays three
  branches per operation. `mispredicts` follows the branch column, up to +20% on
  `bench/arith.algo` isolated, and that is valgrind's predictor model rather than
  this machine's.

  (iii) The per-operator costs are countable in the disassembly — 8, 10, 12, 14
  and 16 instructions for `+ - * / <` in the chain against 8, 10, 6, 9 and 9 in
  the switch — and multiplying them by the operator counts each benchmark source
  implies predicts all four programs, in both series, on both columns, to within
  41 counts in 46 million on `branches`. So the *attribution* is unusually
  strong even though the *effect* is unusually small, and the two statements are
  independent. `results/README.md`'s item-3.3 section carries the tables.

  **A fourth point belongs here and is a boundary rather than a consequence.**
  These per-operator costs are properties of one compiler at one optimisation
  level. A compiler that emitted the source's chain literally, or that turned the
  switch into a jump table, would give C a different and probably much larger
  number on the same source. Configuration N's costs are what *this* toolchain
  produced, and the whole series is measured under it, so the deltas are
  internally consistent — but C's figure is the one in the series that would
  travel worst to another toolchain.

- **Ablation C changed two node sizes and moved no cache column, which is the
  same check as B's and the opposite answer.** Left by item 3.3. `BinOpNode`
  goes from 80 bytes to 88 and `UnaryOpNode` from 64 to 72, so the check the B
  entry above installed — *look at node size before attributing a cache movement*
  — was run expecting a repeat of B's −15.57% on `bench/fib32.algo`. The largest
  movement in C's eight rows is 485 `D1_misses` out of 13.3 million.

  Measured rather than argued: overriding `operator new` and building a node each
  way, `make_shared<BinOpNode>` requests **96 bytes before C and 104 after, and
  glibc's `malloc` returns a 104-byte usable block for both**; `UnaryOpNode` is
  80 against 88 requested and 88 usable either way. The eight bytes fit inside
  slack the allocator was already handing out, so every node's heap footprint is
  byte-identical across C.

  **The rule that follows is narrower than "check the node size".** A size change
  matters to the cache columns only when it changes the *allocation* size, and
  `make_shared` plus glibc's 16-byte granularity means a node can grow without
  that happening. Item 3.4 should check the allocated block, not `sizeof`.

- **Ablation D makes `bench/fib32.algo`'s D1 misses go UP by a third, and the
  cause is a glibc size class rather than anything the ablation claims to do.**
  Left by item 3.4, and measured rather than argued, in the same way item 3.3
  settled why C's eight extra bytes moved no cache column at all.

  D removes a quarter of `fib32`'s data traffic — `Dr` + `Dw` from 7,557,008,870
  to 5,596,015,408 — and its `D1_misses` rise from 13,294,533 to **17,636,152**,
  +32.66% isolated and +36.30% on top of A, B and C. Nothing comparable happens
  to the other three programs, which move by single counts. The discriminator is
  that `fib32` is the only program that pushes and pops a frame per call, 7.05
  million times.

  Per function, the map's own misses do go away — `map::operator[]` −1,448,348,
  `_Rb_tree::_M_emplace_hint_unique` −608,415, `bcmp` −227,828 — and more than
  that reappears in `Interpreter::callFunction` (+2,551,914) and
  `Interpreter::executeStatement` (+2,261,354), which are the functions that
  now touch the frame directly.

  **The mechanism is that D puts two of a call's allocations into one size
  class.** In configuration N a call allocates its `arguments` vector (16 bytes
  requested, 24 usable) and its frame's red-black-tree node (80 requested, 88
  usable): two size classes, two free lists, so glibc hands the frame allocation
  the same block at each recursion depth. In D the frame is a
  `std::vector<Value>` of one element — 16 requested, 24 usable — which is
  *`arguments`' own size class*, so both draw on one tcache bin and interleave.
  Walking `fib(24)`'s call tree, 150,049 calls, under each allocation pattern
  and counting the distinct addresses the frame allocation is ever handed:

  | pattern | distinct frame blocks | distinct argument blocks |
  |---|---|---|
  | N — 16 bytes + 80 bytes | **24** | 24 |
  | D — 16 bytes + 16 bytes | **47** | 47 |

  One per live recursion depth in N, roughly two in D. The frame block got
  *smaller* and the number of distinct lines a call touches roughly doubled.

  **Three consequences.** This is now the fourth mechanism on this list that
  moves `D1_misses` without moving `Ir` — after the environment block,
  `argv[0]`'s length, and node size — and the fourth independent reason the
  attribution is stated on `Ir`. It is **not something to fix**: padding the
  frame vector into another size class would be a second change wearing D's
  tag, with no row to attribute it to, which is the rule items 3.1, 3.2 and 3.3
  each applied in turn. And it is a reminder that a *smaller* allocation is not
  automatically a better-behaved one — what the cache sees is how many distinct
  blocks a workload cycles through, not how large each is.

- **After ablation D a release build has no `undefined variable` fault, and the
  invariant behind it is checked by an `assert` that `NDEBUG` removes from every
  measured configuration.** Left by item 3.4. It is a boundary rather than a
  result, and it is stated because the trade was deliberate and has a cost.

  Until D, `Interpreter::evaluate` looked the name up in the environment and
  raised a `RuntimeFault` if it was absent. The lookup was unreachable for any
  program the resolver accepts — item 1.3 closed the last hole — and it survived
  on two arguments: it *was* ablation D, so deleting it early would have
  performed half the ablation; and a fault beats undefined behaviour if a later
  item ever widens what the resolver admits. D spends the first. The second is
  answered by moving the check to where it is free rather than by keeping it.

  An `assert` is live in the default build — the one `ctest` runs, the one CI
  compiles under GCC and Clang, the one `-DALGO_SANITIZE=ON` extends with UBSan
  — and is compiled out of the `RelWithDebInfo` build that produces every row
  here. Confirmed rather than assumed: both measured binaries were built with
  `-O2 -g -DNDEBUG`, and neither contains the string `__assert_fail` or
  `Assertion` anywhere, where the host's default build does.

  **What was rejected and why.** A surviving bounds test — `at()`, or an
  explicit range check — is a comparison and a conditional branch on the hot
  path, present in D and absent in N, roughly one per variable access, which on
  `bench/vars.algo` is 23 per loop iteration. D would then have removed the map
  lookup *and* added a check, while this repository recorded only the removal.
  That is the rule item 3.1 set choosing `const Node &` over a raw pointer.

  **What is genuinely lost, stated plainly.** In an optimised build an
  unresolved slot is now undefined behaviour where it used to be a diagnostic
  and exit 70. Nothing can reach it today. If a later item widens the resolver,
  the assert fires in every test on both compilers before anything ships —
  provided a test exercises the widened case, which is the part that is not
  automatic. In the build where it lives the assert is *stronger* than the
  lookup it replaced: `find` could only see a name missing from the map, while
  this also sees an unwritten slot and a slot past the end of the frame, which
  is the failure mode a slot-indexed environment newly has and a map could not
  have had.

- **At configuration H every benchmark program falls below item 2.2's 0.5 s
  floor, and Phase 4 will measure below it.** Left by item 3.4. Wall clock
  best-of-ten in the container: `arith` 849 → **203 ms**, `fib32` 869 → **431
  ms**, `loop10m` 660 → **175 ms**, `vars` 900 → **95 ms**, N against H.

  The 0.5–5 s band was item 2.2's acceptance criterion for the *baseline*
  programs, and hardening the interpreter by between 2.0× and 9.5× was always
  going to fall out of it from below. **Nothing measured is invalidated**: the
  wall-clock columns are narrative only, and cachegrind's counts are simulated,
  deterministic and reproduce exactly at any program size. The floor existed to
  keep timing noise small relative to the total, and no claim in this repository
  rests on timing.

  It is recorded because the temptation it creates is real: a Phase 4 session
  measuring a VM on programs that finish in tenths of a second may want to raise
  an `n` to "use the band properly". **Do not** — `CLAUDE.md` refuses it for an
  independent reason, that one cachegrind pass over the four programs costs
  about 230 seconds and Phase 4 adds two more configurations — V and its
  `V-tree` control — to a series that already has nine. If a Phase 4 comparison
  ever needs a longer-running program, that is a new benchmark with its own
  justification, not a larger `n` on an existing one.

- **The whole result is one machine, one operating system, one compiler, one
  optimisation level and four programs the author wrote.** Left by item 5.3, and
  it is the widest boundary on this list — every other entry narrows a figure,
  this one bounds all of them at once.

  *One machine and one operating system.* Every committed row is arm64, taken
  inside the container described at the top of this file, on the CPU the
  *Recorded platform* section names. Nothing was measured on the host, which is
  not a preference — valgrind does not meaningfully support arm64 macOS — and
  nothing was measured on x86_64, which is why item 5.4's CI instruction-count
  gate was cut rather than written: CI runs `ubuntu-latest`, and a count taken on
  one architecture may not be compared to a count taken on the other.

  *One compiler.* GCC 13.3.0. The CI matrix builds and tests under both GCC and
  Clang, so the language's behaviour is checked twice, but CI builds unoptimised
  and measures nothing — **no number in this repository has ever been produced by
  Clang.** Three entries below turn on what GCC chose to emit: A's non-atomic
  reference count, C's character chain in place of the source's string
  comparisons, and the code-generation term in the residual. Item 3.3's is the
  sharpest of them — a compiler that emitted C's chain literally, or that turned
  the `switch` into a jump table, would give C a different and probably much
  larger number **on the same source**.

  *One optimisation level.* `-O2 -g -DNDEBUG`, set in `Dockerfile.bench`. The
  *Optimisation level* section above records why, and records what `-O0` costs.
  An ablation measured unoptimised would be credited with work the compiler
  declined to attempt, and the `NDEBUG` half of that flag is itself a boundary —
  see the `undefined variable` entry below.

  *Four programs, chosen rather than sampled.* `bench/fib32.algo`,
  `bench/loop10m.algo`, `bench/arith.algo` and `bench/vars.algo` were written to
  load four different parts of the interpreter, and they are not a sample of
  anything. They are not representative of any workload, they were not drawn
  from a corpus, and the author knew which ablations were coming when they were
  written. Where a per-visit cost model holds across all four it holds across
  four points of one author's choosing. The programs are also the reason two
  figures in this document travel badly on their own: **D's is a property of a
  20-name frame**, and the H → V figures are dominated by whether a program calls
  a function — `bench/fib32.algo` is the only one of the four that does, and it
  is the only one of the four where the VM wins.

- **Locality is not separated out from the architectural result, so the H → V
  number is an upper bound rather than an attribution.** Left by item 5.3,
  discharging a cost that item 3.5's cut incurred on 2026-08-30.

  Ablation E — arena-allocated AST nodes — was the ablation that would have
  priced node layout and traversal locality inside the tree-walker. It was cut,
  and it stays cut; the reasoning is in the roadmap, and part of it was that a
  bytecode chunk is contiguous by construction, so locality is precisely one of
  the things the architectural substitution delivers for free. That reasoning is
  sound and the price is real: **no row in this repository separates locality
  from dispatch, from node indirection, or from anything else that changes
  between a tree-walker and a VM.** Whatever the contiguous chunk and the
  stack-allocated frames are worth is inside the H → V delta, unlabelled.

  So H → V bounds the architectural effect **from above**, because it contains at
  least one named cause that nothing here isolates. It may be quoted as *what
  replacing the back end did*, never as *what the architecture is worth*. The
  same applies to the direction the result actually runs in: H → V is a **loss**
  on three programs of four in instructions retired — `arith` **+90.29%**,
  `loop10m` **+54.82%**, `vars` **+87.92%** — and a win only on `bench/fib32.algo`
  at **−36.61%**. An upper bound on a mixed result bounds the losses too.

  **And N → V must never be quoted without H → V beside it.** N → V is −44.44%,
  −66.59%, −56.57% and −68.57%, which is a far more flattering set of numbers and
  a dishonest one: it credits the bytecode VM with the four Phase 3 ablations,
  every one of which is a change to the *tree-walker* that the VM did not make
  and does not contain. Measuring an architecture against an unhardened baseline
  is the specific comparison this project exists to refuse, and Phase 3 runs
  before Phase 4 for no other reason.

- **The H → V comparison crosses an argv-and-binary boundary, and only its
  instruction column survives that crossing intact.** Left by item 5.1, and
  priced rather than argued.

  H and V are not the same binary invoked the same way. V's `argv` gains
  `--engine=vm`, and V's binary links three translation units H's does not — the
  Phase 4 back end: `src/compiler.cpp`, `src/vm.cpp` and `src/disassembler.cpp`,
  which take `algo_core` from five sources to eight. Two mechanisms already on
  this list say that both of those can move a cache column without any change in
  the work done: the environment
  and argv block shifts the initial process layout, and the length of the path
  the binary is invoked by moves `bench/fib32.algo`'s D1 misses by 9.6% for
  fourteen characters.

  **`V-tree` is the control that prices it**, and it is the strongest number item
  5.1 produced: the same worktree binary at the same 38-character path, invoked
  with `--engine=tree`, so that everything except the engine selected is held
  fixed. It reproduces H's instruction counts to a **fixed +4,053** on `arith`,
  `fib32` and `loop10m` and **+4,017** on `vars` — a constant, not a rate, which
  is startup and one argv element and no per-iteration work at all. Against
  deltas in the billions that is nothing, so **`Ir` carries the H → V step
  directly.**

  The cache columns do not get the same pass. **The fully controlled cache
  comparison is V against V-tree, not V against H** — for example the VM's
  removal of **99.83%** of `bench/fib32.algo`'s D1 misses is 20,593 against
  V-tree's 11,901,112, and it is stated that way in `results/README.md` for this
  reason. Reading V's cache columns against H's would fold the argv difference
  and the extra translation units into the architectural result.

  A side effect worth recording: this is also the first *counter-based* check on
  Phase 4's claim to have measured nothing and touched no interpreter source. A
  fixed four-thousand-count offset across four programs is what that claim
  predicts, and it is what the rows show.

- **The cycle-level half of the mechanism was never measurable on this platform,
  and no part of it was estimated.** Left by item 5.2 and written here by item
  5.3, because a reader is entitled to know which half of an explanation rests on
  a column that does not exist.

  `perf_cycles`, `perf_instructions` and `perf_ipc` are **empty in all 68 rows**,
  and they always were. `perf` does not run here: the packaged tools are built for
  kernel 6.8 and Docker Desktop runs LinuxKit 6.12.68, so it exits 2. Item 2.3
  treated `perf` as optional and every number the attribution rests on is
  simulated, which is why this blocks nothing — but it does bound what may be
  said.

  The roadmap's predicted mechanism for a positive interaction residual is
  **memory stalls hiding each other's cost**, which is a claim about *cycles*.
  Item 5.2 reported that mechanism as **predicted but not observed**, and the
  evidence for "not observed" is flatness in columns that do exist — `LL_misses`
  staying inside a band of tens of counts across all nine Phase 3 configurations
  on programs retiring 12.8 to 16.0 billion instructions, and `arith` and
  `loop10m` D1 misses moving by single digits while their instruction counts fall
  over 70%. **That is an absence of the traffic a stall would need, not a
  measurement of stalls.** No IPC was estimated, and none may be: dividing a
  committed `Ir` by the wall-clock column would manufacture exactly the number
  the item declined to claim, from a column this document reports as narrative
  only.

  Two smaller consequences of the same gap. The residual's positive sign is
  attributed to **code generation**, which is legitimate precisely because it is
  visible in instructions retired, where nothing can hide behind a stall — but
  that attribution establishes a second source, not the absence of the first.
  And the `mispredicts` column does **not** decompose across the four ablations
  the way `branches` does; `branches` has a residual of exactly zero on all four
  programs and `mispredicts` is mixed in sign, so the branch column's additivity
  is a property of these four ablations rather than of any predictor — and the
  predictor in question is valgrind's model, not this machine's, as the first
  entry in this section says of every cache and branch figure here.
