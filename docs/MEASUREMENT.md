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
`Dockerfile.bench`: `scripts/bench.sh` is item **2.3**'s and will supersede it.

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
  20,688). Map machinery plus string-key comparison is somewhere between **23%
  and 56%** of its instructions; the spread is only how much of `bcmp` serves map
  keys rather than the operator-dispatch chain, and ablation D is itself the
  instrument that resolves it.

  So D's number is reportable as measured, but **must be reported as "on a
  20-name frame"** rather than as what the environment costs in general. The
  names are also short (`v01`…`v16`), which understates the effect rather than
  inflating it, since longer keys cost more per comparison.
