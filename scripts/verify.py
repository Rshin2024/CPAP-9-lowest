#!/usr/bin/env python3
"""Verify CPAP hits with difference 210 and 2 <= T <= 10.

Targets require both gmpy2 probable-prime tests and PARI/GP isprime proofs.
Every intermediate must fail gmpy2's primality test. Exit status is 0 only
when every requested hit is verified, 1 for an invalid hit, and 2 for errors.
"""
import argparse
import re
import subprocess
import sys

from gmpy2 import mpz, is_prime


class VerificationError(RuntimeError):
    """A required primality proof could not be completed."""


def pari_isprime(n):
    try:
        result = subprocess.run(
            ["gp", "-q", "-f"], input=f"print(isprime({n}))\nquit\n",
            capture_output=True, text=True, timeout=600,
        )
    except subprocess.TimeoutExpired as exc:
        raise VerificationError("PARI/GP proof timed out") from exc
    except OSError as exc:
        raise VerificationError(f"cannot run PARI/GP: {exc}") from exc
    answer = result.stdout.strip()
    if result.returncode or result.stderr.strip() or answer not in {"0", "1"}:
        detail = result.stderr.strip() or repr(answer)
        raise VerificationError(f"PARI/GP proof failed (exit {result.returncode}): {detail}")
    return answer == "1"


def validate_parameters(N, T):
    if not isinstance(T, int) or isinstance(T, bool) or not 2 <= T <= 10:
        raise ValueError("T must be an integer from 2 through 10")
    if (isinstance(N, bool) or not isinstance(N, (str, int, mpz))
            or isinstance(N, str) and re.fullmatch(r"[+-]?[0-9]+", N) is None):
        raise ValueError("N must be an integer greater than 1")
    try:
        N = mpz(N)
    except (TypeError, ValueError) as exc:
        raise ValueError("N must be an integer greater than 1") from exc
    if N <= 1:
        raise ValueError("N must be an integer greater than 1")
    return N


def parse_hit_line(line):
    """Return (N, T), skip ordinary log lines, and reject broken HIT records."""
    marker = re.search(r"\bHIT(?=\s|$)", line)
    if marker is None:
        return None
    text = line[marker.start():].strip()
    match = re.match(r"HIT CPAP-([0-9]+)(?=\s|$)", text)
    values = re.findall(r"(?:^|\s)N=([^\s]+)", text)
    if (match is None or len(values) != 1
            or re.fullmatch(r"[0-9]+", values[0]) is None
            or len(re.findall(r"\bHIT(?=\s|$)", text)) != 1):
        raise ValueError("malformed HIT record")
    T = int(match.group(1))
    N = validate_parameters(values[0], T)
    return str(N), T


def read_hits(path):
    entries = []
    with open(path) as source:
        for number, line in enumerate(source, 1):
            try:
                hit = parse_hit_line(line)
            except ValueError as exc:
                raise ValueError(f"{path}:{number}: {exc}") from exc
            if hit is not None:
                entries.append(hit)
    if not entries:
        raise ValueError(f"{path}: no hits to verify")
    return entries


def verify(N, T):
    N = validate_parameters(N, T)
    ok = True
    report = []
    for i in range(T):
        v = N + 210 * i
        prp = bool(is_prime(v, 40))
        proved = pari_isprime(v) if prp else False
        pari = "proved" if proved else ("COMPOSITE" if prp else "not run")
        report.append(f"  target {i}: BPSW/MR={'prime' if prp else 'COMPOSITE'} PARI-isprime={pari}")
        if not prp or proved is not True:
            ok = False
    bad = [j for j in range(1, 210 * (T - 1))
           if j % 210 and is_prime(N + j, 25)]
    if bad:
        ok = False
        report.append(f"  PRIME OR PROBABLE-PRIME INTERMEDIATES at offsets {bad}")
    else:
        report.append(f"  all {209 * (T - 1)} intermediate numbers are composite")
    report.append(f"  digits={len(str(N))}  N mod 210 = {N % 210}")
    return ok, report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="hits file or decimal N")
    parser.add_argument("T", nargs="?", type=int, default=9,
                        help="progression length for decimal N (default: 9)")
    args = parser.parse_args(argv)
    try:
        if not 2 <= args.T <= 10:
            raise ValueError("T must be an integer from 2 through 10")
        if re.fullmatch(r"[+-]?[0-9]+", args.input):
            entries = [(str(validate_parameters(args.input, args.T)), args.T)]
        else:
            entries = read_hits(args.input)
        all_ok = True
        for N, T in entries:
            ok, report = verify(N, T)
            all_ok = all_ok and ok
            print(f"{'VERIFIED' if ok else 'INVALID'} CPAP-{T} N={N}")
            print("\n".join(report))
        return 0 if all_ok else 1
    except (OSError, ValueError, VerificationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
