/*
 * cover.c - covering-set optimizer for CPAP-9 (difference 210) searches.
 *
 * We look for N = k*M + x with the 9 targets N+210*i (i=0..8) prime and the
 * 1672 numbers in between composite.  M = 210 * prod(primes 11..P).  The
 * residue x mod p (p | M, p >= 11) decides which residue class of offsets
 * j (1..1680) is forced composite by p: p | N+j  <=>  j == b_p (mod p),
 * where b_p = -x mod p.  The class must avoid the targets: b_p != 210*i mod p.
 *
 * Offsets j with gcd(x+j, 210) = 1 (and j not a multiple of 210) are the
 * 376 "hard" positions; the rest are composite because of 2,3,5,7.
 * Goal: choose x mod 210 (r) and all b_p to minimize the number of hard
 * positions left uncovered.  Simulated annealing with incremental updates.
 *
 * usage: cover P [iters] [restarts] [seed] [slack] [exclude primes...]
 *   prints best configurations found as lines:
 *   CFG unc=<u> r=<r> p1:b1 p2:b2 ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#define MAXP 64
#define NPOS 1681

static uint64_t number(const char *s, uint64_t maximum) {
    char *end; errno = 0;
    if (!s[0] || s[0] < '0' || s[0] > '9') { fprintf(stderr, "expected a nonnegative integer\n"); exit(1); }
    unsigned long long n = strtoull(s, &end, 10);
    if (errno || *end || n > maximum) { fprintf(stderr, "integer argument out of range\n"); exit(1); }
    return (uint64_t)n;
}

static int primes[MAXP], np;
static int hard[NPOS], nhard;       /* hard offsets j */
static int hidx[NPOS];              /* offset -> index in hard[] or -1 */
static int allowed[MAXP][512], nallowed[MAXP];
/* class members: for prime index pi and residue b, list of hard indices */
static int *cls[MAXP][512];
static int clsn[MAXP][512];
static int clsbuf[MAXP * NPOS];

static uint64_t rng_s[2];
static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
static uint64_t rnd(void) {
    uint64_t s0 = rng_s[0], s1 = rng_s[1], r = s0 + s1;
    s1 ^= s0; rng_s[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); rng_s[1] = rotl(s1, 36);
    return r;
}
static double rndf(void) { return (rnd() >> 11) * (1.0 / 9007199254740992.0); }

static int isprime_small(int n) {
    if (n < 2) return 0;
    for (int d = 2; d * d <= n; d++) if (n % d == 0) return 0;
    return 1;
}
static int gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

static void setup_r(int r) {
    nhard = 0;
    for (int j = 0; j < NPOS; j++) hidx[j] = -1;
    for (int j = 1; j <= 1680; j++) {
        if (j % 210 == 0) continue;
        if (gcd(r + j, 210) != 1) continue;
        hidx[j] = nhard; hard[nhard++] = j;
    }
    int *bp = clsbuf;
    for (int pi = 0; pi < np; pi++) {
        int p = primes[pi];
        for (int b = 0; b < p; b++) {
            cls[pi][b] = bp; clsn[pi][b] = 0;
            for (int h = 0; h < nhard; h++) if (hard[h] % p == b) { *bp++ = h; clsn[pi][b]++; }
        }
        nallowed[pi] = 0;
        for (int b = 0; b < p; b++) {
            int bad = 0;
            for (int i = 0; i < 9; i++) if ((210 * i) % p == b) { bad = 1; break; }
            if (!bad) allowed[pi][nallowed[pi]++] = b;
        }
    }
}

static int cnt[NPOS];
static int curb[MAXP];

static int eval_full(void) {
    memset(cnt, 0, sizeof(int) * nhard);
    for (int pi = 0; pi < np; pi++) {
        int *c = cls[pi][curb[pi]];
        for (int t = 0; t < clsn[pi][curb[pi]]; t++) cnt[c[t]]++;
    }
    int u = 0;
    for (int h = 0; h < nhard; h++) if (cnt[h] == 0) u++;
    return u;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: cover P [iters] [restarts] [seed] [slack] [exclude...]\n"); return 1; }
    int P = (int)number(argv[1], 511);
    long iters = argc > 2 ? (long)number(argv[2], LONG_MAX) : 2000000;
    int restarts = argc > 3 ? (int)number(argv[3], INT_MAX) : 20;
    uint64_t seed = argc > 4 ? number(argv[4], UINT64_MAX) : (uint64_t)time(NULL);
    int slack = argc > 5 ? (int)number(argv[5], NPOS) : 0;
    if (P < 11 || !iters) { fprintf(stderr, "require 11 <= P <= 511 and positive iters\n"); return 1; }
    int excl[64], nexcl = 0;
    for (int a = 6; a < argc; a++) { if (nexcl >= MAXP) { fprintf(stderr, "too many excluded primes\n"); return 1; } excl[nexcl++] = (int)number(argv[a], 511); }
    np = 0;
    for (int p = 11; p <= P; p++) if (isprime_small(p)) {
        int ex = 0; for (int e = 0; e < nexcl; e++) if (excl[e] == p) ex = 1;
        if (!ex) { if (np >= MAXP) { fprintf(stderr, "too many modulus primes (maximum 64)\n"); return 1; } primes[np++] = p; }
    }
    if (!np) { fprintf(stderr, "at least one modulus prime is required\n"); return 1; }
    rng_s[0] = seed * 0x9E3779B97F4A7C15ULL + 1; rng_s[1] = seed ^ 0xD1B54A32D192ED03ULL;
    for (int i = 0; i < 10; i++) rnd();

    int rs[48], nr = 0;
    for (int r = 1; r < 210; r++) if (gcd(r, 210) == 1) rs[nr++] = r;

    int gbest = 1 << 30;
    for (int rep = 0; rep < restarts; rep++) {
        int r = rs[rnd() % nr];
        setup_r(r);
        for (int pi = 0; pi < np; pi++) curb[pi] = allowed[pi][rnd() % nallowed[pi]];
        int unc = eval_full();
        int best = unc; int bestb[MAXP]; memcpy(bestb, curb, sizeof(bestb));
        double T0 = 2.0, T1 = 0.05;
        for (long it = 0; it < iters; it++) {
            double T = T0 * pow(T1 / T0, (double)it / iters);
            int pi = rnd() % np;
            int nb = allowed[pi][rnd() % nallowed[pi]];
            int ob = curb[pi];
            if (nb == ob) continue;
            int delta = 0;
            int *c = cls[pi][ob];
            for (int t = 0; t < clsn[pi][ob]; t++) if (cnt[c[t]] == 1) delta++;
            c = cls[pi][nb];
            for (int t = 0; t < clsn[pi][nb]; t++) if (cnt[c[t]] == 0) delta--;
            if (delta <= 0 || rndf() < exp(-delta / T)) {
                c = cls[pi][ob];
                for (int t = 0; t < clsn[pi][ob]; t++) cnt[c[t]]--;
                c = cls[pi][nb];
                for (int t = 0; t < clsn[pi][nb]; t++) cnt[c[t]]++;
                curb[pi] = nb; unc += delta;
                if (unc < best) { best = unc; memcpy(bestb, curb, sizeof(bestb)); }
            }
        }
        if (best < gbest) gbest = best;
        if (best <= gbest + slack) {
            printf("CFG unc=%d r=%d", best, r);
            for (int pi = 0; pi < np; pi++) printf(" %d:%d", primes[pi], bestb[pi]);
            printf("\n");
            fflush(stdout);
        }
        fprintf(stderr, "restart %d r=%d best=%d global=%d\n", rep, r, best, gbest);
    }
    return 0;
}
