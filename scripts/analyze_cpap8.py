#!/usr/bin/env python3
"""Coverage analysis of the CPAP-8 record 25483976638 * 97# + x36 (Sep 2026)."""
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
k = 25483976638
N = k * M + x36
print("digits:", len(str(N)), " log10(M)=%.3f" % float(gmpy2.log10(M)))
print("8 targets prime:", all(is_prime(N + 210 * i, 25) for i in range(8)))
unc = [j for j in range(1, 1471) if j % 210 and gcd(N + j, M) == 1]
print("intermediates composite:", all(not is_prime(N + j, 5) for j in range(1, 1471) if j % 210))
print("uncovered (coprime to 97#):", len(unc))
hard = [j for j in range(1, 1471) if j % 210 and gcd(x36 + j, 210) == 1]
print("hard positions:", len(hard), " x36 mod 210 =", x36 % 210)
