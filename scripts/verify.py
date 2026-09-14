#!/usr/bin/env python3
"""Independently verify CPAP hits.

usage: verify.py <hits-file | decimal N> [T]

For each hit: the T numbers N + 210*i must be prime (proved with PARI/GP's
APR-CL `isprime`, plus gmpy2 BPSW/Miller-Rabin), and every N + j with
0 < j < 210*(T-1), j not a multiple of 210, must be composite (a failed
Miller-Rabin test is a proof of compositeness)."""
import re, subprocess, sys
import gmpy2
from gmpy2 import mpz, is_prime

def pari_isprime(n):
    try:
        out = subprocess.run(["gp", "-q", "-f"], input=f"print(isprime({n}))\nquit\n",
                             capture_output=True, text=True, timeout=600).stdout.strip()
        return out.split()[-1] == "1"
    except Exception as e:
        return None

def verify(N, T):
    N = mpz(N)
    ok = True
    report = []
    for i in range(T):
        v = N + 210 * i
        prp = is_prime(v, 40)
        proved = pari_isprime(v)
        report.append(f"  target {i}: BPSW/MR={'prime' if prp else 'COMPOSITE'} PARI-isprime={'proved' if proved else ('FAILED' if proved is False else 'n/a')}")
        if not prp or proved is False:
            ok = False
    bad = []
    for j in range(1, 210 * (T - 1)):
        if j % 210 == 0:
            continue
        if is_prime(N + j, 25):
            bad.append(j)
    if bad:
        ok = False
        report.append(f"  PRIME INTERMEDIATES at offsets {bad}")
    else:
        report.append(f"  all {210*(T-1) - 1 - (T-2)} intermediate numbers are composite")
    report.append(f"  digits={len(str(N))}  N mod 210 = {N % 210}")
    return ok, report

def main():
    arg = sys.argv[1]
    T = int(sys.argv[2]) if len(sys.argv) > 2 else 9
    entries = []
    if arg.isdigit():
        entries.append((arg, T))
    else:
        for line in open(arg):
            m = re.search(r"HIT CPAP-(\d+) .*?N=(\d+)", line)
            if m:
                entries.append((m.group(2), int(m.group(1))))
    if not entries:
        print("no hits to verify"); return
    for N, t in entries:
        ok, rep = verify(N, t)
        print(f"{'VERIFIED' if ok else 'INVALID'} CPAP-{t} N={N}")
        print("\n".join(rep))

if __name__ == "__main__":
    main()
