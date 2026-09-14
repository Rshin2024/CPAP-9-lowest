#!/usr/bin/env python3
"""Which small primes divide the intermediate numbers of the known records?
If primes beyond the stated primorial divide the 'uncovered' intermediates far
more often than chance, the actual search used a larger modulus."""
import gmpy2
from gmpy2 import mpz, is_prime, gcd
from collections import Counter

def primorial(n):
    r = mpz(1)
    for p in range(2, n + 1):
        if is_prime(p):
            r *= p
    return r

def analyze(name, N, kk, Mprimes, maxq=1000):
    print("==", name)
    L = 210 * (kk - 1)
    inter = [j for j in range(1, L) if j % 210]
    M = mpz(1)
    for p in Mprimes: M *= p
    unc = [j for j in inter if gcd(N + j, M) == 1]
    print(" uncovered wrt stated modulus:", len(unc))
    # smallest prime factor of each uncovered intermediate
    spf = {}
    for j in unc:
        v = N + j
        f = None
        q = 2
        while q < 200000:
            if v % q == 0: f = q; break
            q = int(gmpy2.next_prime(q))
        spf[j] = f
    c = Counter(spf.values())
    print(" smallest prime factor distribution of uncovered intermediates:")
    print("  ", sorted(c.items(), key=lambda t: (t[0] is None, t[0]))[:60])
    # For each prime q in (P, 400], how many intermediates (all) does it divide?
    print(" primes q dividing intermediates (count, expected ~%.1f per q=1672/q):" % 0)
    for q in range(max(Mprimes) + 1, 400):
        if not is_prime(q): continue
        cnt = sum(1 for j in inter if (N + j) % q == 0)
        exp = len(inter) / q
        flag = " <==" if cnt > 2.5 * exp else ""
        print("  q=%3d divides %3d intermediates (expected %.1f)%s" % (q, cnt, exp, flag))

# CPAP-8 record (Sep 2026)
M8 = primorial(97)
x36 = mpz("836699507882418031308586693292191597")
N8 = 25483976638 * M8 + x36
analyze("CPAP-8: 25483976638*97# + x36", N8, 8, [p for p in range(2, 98) if is_prime(p)])

# CPAP-9 record (2004)
M9 = primorial(179) // (149 * 157)
x65 = mpz("87103490338886343449123705322656962705040008760706629856986802283")
N9 = 3416716311814 * M9 + x65
analyze("CPAP-9: 3416716311814*179#/(149*157) + x65", N9, 9, [p for p in range(2, 180) if is_prime(p) and p not in (149, 157)])
