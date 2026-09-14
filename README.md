# CPAP-9-lowest
Smallest known CPAP-9 search

A CPAP-k is k *consecutive* primes in arithmetic progression. For k = 8, 9, 10 the
common difference must be a multiple of 210, so a CPAP-9 needs 9 primes
`N, N+210, ..., N+1680` with all 1672 numbers in between composite.

The smallest known CPAP-9 (Rosenthal & Andersen, 2004) has 79 digits:
`3416716311814 * 179#/(149*157) + x65 + 210n`. This repository contains the
tooling used to search for smaller ones.

## Method

Numbers are searched in the form `N = k*M + x` with `M = 210 * 11 * 13 * ... * P`.

1. **Covering optimizer** (`src/cover.c`, also embedded in `src/search.c`):
   the residues of `x` modulo each prime `p | M` decide which residue class of
   offsets `j` is forced composite by `p`. Simulated annealing chooses
   `x mod 210` and all residues so that the 9 targets stay coprime to `M`
   while as many as possible of the 376 "hard" intermediate offsets (those
   coprime to 210) are covered. With primes up to 139 about 110 offsets remain
   uncovered; each one survives as composite with probability ~0.94.
2. **Sieve** (`src/search.c`): for each offset `x`, `k` is sieved over
   `[1, K)` with all primes `q <= 2^18` not dividing `M` (9 residue classes
   per `q`), at ~1.5e8 k/s per core.
3. **PRP + verification**: survivors get Fermat tests on the 9 targets, then
   the uncovered intermediates are checked, then every intermediate; hits are
   confirmed with strong Miller-Rabin and (in `scripts/verify.py`) proved with
   PARI/GP `isprime` (APR-CL).

Instead of one offset with a huge `k` range, many near-optimal offsets are each
searched over a short `k` range, which keeps `N` close to `M` in size.
Expected cost with `M = 139#` is ~47 VM-hours (4 cores) per CPAP-9 of about
63 digits; with `M = 127#` about 85 VM-hours per ~56-digit CPAP-9.

Two offset-generation modes exist:

* **Annealing mode** (default): every process anneals its own coverings and
  derives single-residue variants (`-v`). Because the annealer keeps
  rediscovering the same optimal coverings, each process searches a disjoint
  `k` range per (offset, repeat count) (`-g index -G count`), so no `(x, k)`
  pair is ever searched twice.
* **Bases mode** (used when `src/bases_<P>.txt` exists): a shared, sorted list
  of base coverings (from `bin/cover`) is expanded deterministically into all
  1–3-residue-swap variants with at most `MAXUNC` uncovered offsets, deduplicated
  and sorted by quality (~15M offsets for P=127). Process `g` of `G` handles the
  variants with index ≡ g (mod G), and `k` is swept in layers `[1,KX)`,
  `[KX,2KX)`, … over the whole list, so hits appear with the smallest `k`
  first. Header fields: `SWAPS`, `QUOTA` (variants per base), `MAXVAR`,
  `LAYER0`, `LAYERS`, `REMAP` (branch-suffix → worker id).

Results so far (see `results/RESULTS.md` for full numbers and APR-CL proofs):

* **48 digits** — `20963748 * 103# + x41 + 210n` (generation 8, bases mode,
  k-layer 2). Current smallest known CPAP-9; 31 digits below the 2004 record.
* 52 digits — `30671858 * 109# + x43 + 210n` (generation 7, bases mode).
* 54 digits — `19415540 * 113# + x46 + 210n` (generation 6, bases mode).
* 57 digits — `52695495 * 127# + x48 + 210n` (generation 5, bases mode).
* 64 digits — `414754040 * 139# + x55 + 210n` (generation 3, annealing mode).

Generation 9 targets P=101 using `src/bases_101.txt`.

## Running

```bash
scripts/setup.sh                       # installs libgmp-dev, pari-gp, builds bin/
./bin/search -P 139 -k 1 -K 200000000 -u 111 -i 1000000 -s 12345 -o hits.txt
```

Reproduce the current records:

```bash
./bin/search -P 179 -X 149,157 -x 87103490338886343449123705322656962705040008760706629856986802283 -k 3416716000000 -K 3416717000000
./bin/search -T 8 -P 97 -x 836699507882418031308586693292191597 -k 25483000000 -K 25484000000
python3 scripts/verify.py <decimal N> 9
```

Distributed run: `scripts/worker.sh <id> [max_hours]` starts one search per
core using the parameters in `control/plan.txt` on `origin/main` (re-read every
2 minutes; changing `GEN` restarts the searches, `P=0` stops), and commits
`results/worker_<id>/{hits.txt,summary.txt,verified.txt}` to the worker's
branch every 15 minutes. `scripts/collect.sh` aggregates all worker branches.

[README.md](https://github.com/user-attachments/files/32209350/README.md)
