#!/usr/bin/env python3
"""Measure the true per-prp9 hit probability for the P=127 search:
 rho = prime density among integers coprime to 127# near N ~ 10^56,
 then P(hit|prp9) = (1-rho)^u for the average uncovered count u=123.76."""
import random, math
import gmpy2
from gmpy2 import mpz, is_prime, gcd

def primorial(P):
    r = mpz(1)
    for p in range(2, P + 1):
        if is_prime(p):
            r *= p
    return r

M = primorial(127)
base = mpz(10) ** 56
# sample integers coprime to M near base, measure prime fraction (BPSW)
random.seed(12345)
trials = 200000
coprime = 0
prime = 0
while coprime < trials:
    n = base + mpz(random.getrandbits(190))
    if gcd(n, M) != 1:
        continue
    coprime += 1
    if is_prime(n):
        prime += 1
rho = prime / coprime
lnN = float(gmpy2.log(base))
cS = 1.0
for p in range(2, 128):
    if is_prime(p): cS *= p / (p - 1)
print(f"measured rho = {rho:.5f}  (theory c_S/lnN = {cS/lnN:.5f}, c_S={cS:.3f}, lnN={lnN:.1f})")
for u in (117, 120, 123.76, 124):
    ph = (1 - rho) ** u
    print(f"  u={u}: P(hit|prp9) = (1-rho)^u = {ph:.3e}  -> 1 hit per {1/ph:.0f} prp9")
print(f"At 17699 prp9 with avg u=123.76: expected hits = {17699*(1-rho)**123.76:.2f}")
