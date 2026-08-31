# Algo — the measurement platform

> **Scope of this file.** Roadmap item **2.1** requires the image digest, the CPU
> model and the compiler version to be written down, because every published
> number is meaningless without them. That is what is here, together with the
> decisions 2.1 had to settle before any number could be taken at all.
> The **Boundary of the claim** section at the end is item **5.3**'s to fill in;
> it is stubbed here so that it has somewhere to land rather than being bolted
> on after the numbers exist.

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
   optimised configuration is tested too: `ctest --test-dir build-bench` inside
   the container passes **32/32**.
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

> **Item 5.3 fills this in.** It is stubbed here so the limitations sit in the
> same file as the platform that produced them.

Two entries already belong here and are recorded now so they are not lost:

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
