#!/usr/bin/env python3
"""Render a HIT line (from results/*/hits.txt) as a record entry and re-verify it.

usage: report_hit.py "<HIT line>"   or   report_hit.py <hits-file>
"""
import re, sys, subprocess
import gmpy2
from gmpy2 import mpz, is_prime, gcd

def primorial(P):
    r = mpz(1)
    for p in range(2, P + 1):
        if is_prime(p): r *= p
    return r

def report(line):
    m = re.search(r"HIT CPAP-(\d+) k=(\d+) digits=(\d+) N=(\d+) x=(\d+)", line)
    if not m:
        return
    T, k, digits, N, x = int(m.group(1)), int(m.group(2)), int(m.group(3)), mpz(m.group(4)), mpz(m.group(5))
    # recover the primorial modulus: M = (N - x) / k must equal P# for some P
    M = (N - x) // k
    assert (N - x) % k == 0
    P = None
    for cand in range(11, 400):
        if is_prime(cand) and primorial(cand) == M:
            P = cand; break
    Mdesc = f"{P}#" if P else str(M)
    print(f"## CPAP-{T} with {digits} digits")
    print()
    print(f"    {k} * {Mdesc} + x{len(str(x))} + 210 n,   n = 0..{T-1}")
    print()
    print(f"x{len(str(x))} = {x}")
    print()
    print(f"N = {N}")
    print()
    print("Primes:")
    for i in range(T):
        print(f"  n={i}: {N + 210*i}")
    print()
    # covering description
    if P:
        res = []
        for p in range(11, P + 1):
            if is_prime(p):
                res.append(f"{p}:{int((-x) % p)}")
        unc = [j for j in range(1, 210*(T-1)) if j % 210 and gcd(x + j, M) == 1]
        print(f"x mod 210 = {x % 210}; covered residue classes p:b (p | N+j iff j = b mod p): {' '.join(res)}")
        print(f"intermediate offsets not covered by primes of {Mdesc}: {len(unc)}")
        print()
    out = subprocess.run([sys.executable, "scripts/verify.py", str(N), str(T)], capture_output=True, text=True).stdout
    print("Verification (scripts/verify.py):")
    print("\n".join("    " + l for l in out.strip().splitlines()))
    print()

def main():
    arg = sys.argv[1]
    if arg.startswith("HIT"):
        report(arg)
    else:
        for line in open(arg):
            if "HIT" in line:
                report(line)

if __name__ == "__main__":
    main()
