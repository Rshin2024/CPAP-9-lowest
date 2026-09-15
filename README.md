# CPAP-9-lowest

A CPAP-9 consists of nine consecutive primes in arithmetic progression.
This search fixes the common difference at 210: all nine `N + 210*i`
(`i = 0..8`) must be prime and all 1,672 intervening integers composite.

## Results

The smallest found has **48 digits**:

```text
N = 502811815791820948505989265164219187224962352997
```

| Digits | Expression for N |
|---|---|
| 48 | 20963748 × 103# + x41 |
| 52 | 30671858 × 109# + x43 |
| 54 | 19415540 × 113# + x46 |
| 57 | 52695495 × 127# + x48 |
| 64 | 414754040 × 139# + x55 |

See [full numbers and verification](results/RESULTS.md). Here `P#` is the
product of primes through P. The [published CPAP record table](https://www.pzktupel.de/CPAP/mini.php)
lists a 79-digit CPAP-9 dated September 12, 2026. This search does not prove
absolute minimality.

## Method

Search candidates have the form `N = k*M + x`, with M a primorial. A covering
optimizer chooses residues that make many intervening integers composite.
A sieve and probable-prime tests filter candidates; `scripts/verify.py`
requires PARI/GP primality proofs and checks every intermediate integer.

Annealing mode generates coverings; `-g`/`-G` partition work and track repeat
counts within each process lifetime. Bases mode expands shared coverings into up to three
residue swaps, subject to `QUOTA`, `MAXVAR`, and `MAXUNC`, then searches
bounded k-layers. Neither mode is exhaustive or guarantees discovery order.

## Run

```sh
scripts/setup.sh                       # Ubuntu/Debian or macOS with Homebrew
scripts/build.sh                       # rebuild with dependencies installed
scripts/test.sh                        # regression tests and all five results
python3 scripts/verify.py 502811815791820948505989265164219187224962352997 9
./bin/search -P 103 -x 19506961754250869267574127632655931757917 -k 20963747 -K 20963750
```

If setup creates `.venv`, use `.venv/bin/python` for Python commands.
The search supports CPAP lengths 8–10 (`-T`).

`scripts/worker.sh <id> [max_hours]` reads `control/plan.txt` from `origin/main`
every two minutes and publishes worker results to its branch. Workers use
four processes by default (`CPUS_PER_WORKER`); all workers must use the same count.
`scripts/collect.sh` combines local and remote worker results.
