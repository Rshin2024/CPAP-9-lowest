#!/usr/bin/env python3
"""Render verified HIT records: report_hit.py '<HIT line>' or <hits-file>."""
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import sys

from gmpy2 import mpz, is_prime, gcd
from verify import parse_hit_line, VerificationError


@dataclass(frozen=True)
class Hit:
    T: int
    k: int
    N: mpz
    x: mpz
    M: mpz


def primorial(P):
    r = mpz(1)
    for p in range(2, P + 1):
        if is_prime(p):
            r *= p
    return r


def parse_record(line):
    hit = parse_hit_line(line)
    if hit is None:
        return None
    match = re.search(
        r"\bHIT CPAP-([0-9]+) k=([0-9]+) digits=([0-9]+) "
        r"N=([0-9]+) x=([0-9]+)(?=\s|$)", line,
    )
    if match is None:
        raise ValueError("malformed HIT metadata")
    T, k, digits = (int(match.group(i)) for i in range(1, 4))
    N, x = mpz(match.group(4)), mpz(match.group(5))
    if digits != len(str(N)):
        raise ValueError("HIT digits field does not match N")
    if k <= 0 or x >= N or (N - x) % k:
        raise ValueError("HIT must have k > 0 and N = k*M + x with positive integer M")
    return Hit(T, k, N, x, (N - x) // k)


def format_report(hit):
    T, k, N, x, M = hit.T, hit.k, hit.N, hit.x, hit.M
    verifier = Path(__file__).resolve().with_name("verify.py")
    result = subprocess.run(
        [sys.executable, str(verifier), str(N), str(T)],
        capture_output=True, text=True,
    )
    expected = f"VERIFIED CPAP-{T} N={N}"
    if result.returncode or result.stderr.strip() or not result.stdout.startswith(expected + "\n"):
        detail = result.stderr.strip() or result.stdout.strip() or "no verification output"
        raise VerificationError(f"verification failed (exit {result.returncode}): {detail}")

    P = next((p for p in range(11, 400) if is_prime(p) and primorial(p) == M), None)
    Mdesc = f"{P}#" if P else str(M)
    xlabel = f"x{len(str(x))}"
    lines = [f"## CPAP-{T} with {len(str(N))} digits", "",
             f"    {k} * {Mdesc} + {xlabel} + 210 n,   n = 0..{T-1}", "",
             f"{xlabel} = {x}", "", f"N = {N}", "", "Primes:"]
    lines.extend(f"  n={i}: {N + 210*i}" for i in range(T))
    lines.append("")
    if P:
        residues = [f"{p}:{int((-x) % p)}" for p in range(11, P + 1) if is_prime(p)]
        unc = sum(1 for j in range(1, 210*(T-1)) if j % 210 and gcd(x + j, M) == 1)
        lines.extend([
            f"x mod 210 = {x % 210}; covered residue classes p:b (p | N+j iff j = b mod p): {' '.join(residues)}",
            f"intermediate offsets not covered by primes of {Mdesc}: {unc}", "",
        ])
    lines.append("Verification (scripts/verify.py):")
    lines.extend("    " + line for line in result.stdout.strip().splitlines())
    return "\n".join(lines) + "\n"


def report(line):
    hit = parse_record(line)
    if hit is None:
        raise ValueError("no HIT record to report")
    print(format_report(hit))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="a HIT line or hits file")
    args = parser.parse_args(argv)
    try:
        if args.input.lstrip().startswith("HIT "):
            lines = [args.input]
        else:
            lines = Path(args.input).read_text().splitlines()
        hits = []
        for number, line in enumerate(lines, 1):
            try:
                hit = parse_record(line)
            except ValueError as exc:
                raise ValueError(f"line {number}: {exc}") from exc
            if hit is not None:
                hits.append(hit)
        if not hits:
            raise ValueError("no hits to report")
        reports = [format_report(hit) for hit in hits]
        print("\n".join(reports))
        return 0
    except (OSError, ValueError, VerificationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
