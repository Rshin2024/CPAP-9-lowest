#!/usr/bin/env python3
"""Analyze the current smallest-known CPAP-9 record (Rosenthal & Andersen, 2004):
verify it is a CPAP-9 and count how many of the 1672 intermediate numbers are
NOT forced composite by the primes dividing M (i.e. the 'uncovered' positions)."""
import gmpy2
from gmpy2 import mpz, is_prime, gcd

def primorial(n):
    r = mpz(1)
    for p in range(2, n + 1):
        if is_prime(p):
            r *= p
    return r

M = primorial(179) // (149 * 157)
x65 = mpz("87103490338886343449123705322656962705040008760706629856986802283")
k = 3416716311814
N = k * M + x65
print("digits of record N:", len(str(N)))
print("log10(M) = %.3f" % (float(gmpy2.log10(M))))

primes_in_M = [p for p in range(2, 180) if is_prime(p) and p not in (149, 157)]
# targets
targets = [N + 210 * i for i in range(9)]
print("all 9 targets prime:", all(is_prime(t, 25) for t in targets))
# intermediates
comp_ok = True
uncovered = []
for j in range(1, 1681):
    if j % 210 == 0:
        continue
    v = N + j
    if gcd(v, M) == 1:
        uncovered.append(j)
    if is_prime(v, 5):
        comp_ok = False
        print("PRIME intermediate at offset", j)
print("all intermediates composite:", comp_ok)
print("uncovered positions (coprime to M):", len(uncovered))
print(uncovered)
# how many of those are coprime to 210 (the 'hard' ones) -- all of them by construction
print("x65 mod 210 =", x65 % 210)
for p in primes_in_M:
    if p > 7:
        b = (-x65) % p  # covered class j == b mod p
        cnt = sum(1 for j in range(1, 1681) if j % 210 != 0 and gcd(x65 + j, 210) == 1 and j % p == b)
        print(f"  p={p:3d} covers class {b:3d}: {cnt} hard positions")
