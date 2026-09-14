#!/usr/bin/env python3
"""Empirically test the density heuristic on the CPAP-8 modulus 97# with x36:
 - P(target 0 prime) should be about c_S / ln N with c_S = prod p/(p-1), p|M
 - number of primes among the uncovered intermediates should be ~ |U| * rho
"""
import math, random
import gmpy2
from gmpy2 import mpz, is_prime, gcd

def primorial(n):
    r = mpz(1)
    for p in range(2, n + 1):
        if is_prime(p):
            r *= p
    return r

M = primorial(97)
x36 = mpz("836699507882418031308586693292191597")
unc = [j for j in range(1, 1471) if j % 210 and gcd(x36 + j, M) == 1]
cS = 1.0
for p in range(2, 98):
    if is_prime(p): cS *= p / (p - 1)
random.seed(1)
T = 3000
t0 = 0
nprimes = 0
zero = 0
for _ in range(T):
    k = random.randrange(10**10, 10**11)
    N = k * M + x36
    if is_prime(N, 2): t0 += 1
    c = sum(1 for j in unc if is_prime(N + j, 2))
    nprimes += c
    if c == 0: zero += 1
lnN = float(gmpy2.log(N))
rho = cS / lnN
print(f"|U|={len(unc)} c_S={cS:.3f} lnN={lnN:.1f} rho={rho:.4f}")
print(f"P(target0 prime): observed {t0/T:.4f}  predicted {rho:.4f}")
print(f"mean #primes among U: observed {nprimes/T:.2f}  predicted {len(unc)*rho:.2f}")
print(f"P(no prime in U): observed {zero/T:.5f}  predicted {math.exp(-len(unc)*rho):.2e} / {(1-rho)**len(unc):.2e}")
