#!/usr/bin/env python3
"""Expected cost per CPAP-9 hit for candidate prime sets (many-offset strategy)."""
import math
from sympy import primerange
U = {113: 125, 127: 122, 131: 118, 137: 115, 139: 112, 149: 109, 151: 105, 157: 103, 163: 99, 167: 96, 173: 93, 179: 90}
Kx = 1e8           # k-range per offset
rate_vm = 6e8      # k/s per 4-core VM
for P, u in U.items():
    ps = list(primerange(2, P + 1))
    log10M = sum(math.log10(p) for p in ps)
    cS = 1.0
    for p in ps: cS *= p / (p - 1)
    lnN = (log10M + math.log10(Kx)) * math.log(10)
    rho = cS / lnN
    C = math.exp(-36 * 1.0 / (P * math.log(P)))   # 9-tuple correction for primes > P
    d = rho ** 9 * (1 - rho) ** u * C
    hrs = 1 / (d * rate_vm) / 3600
    print(f"P={P:3d} log10M={log10M:5.1f} digits<={log10M+math.log10(Kx):5.1f} |U|={u:3d} rho={rho:.4f} d={d:.2e} VM-hours/hit={hrs:6.1f}")
