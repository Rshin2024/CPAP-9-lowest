/*
 * search.c - sieve + PRP search for CPAP-T (T consecutive primes in
 * arithmetic progression with difference 210), T = 8, 9 or 10.
 *
 * Numbers have the form N = k*M + x, M = 210 * prod(S) where S is a set of
 * primes >= 11.  The residues of x modulo the primes of M are chosen (by a
 * simulated-annealing covering optimizer, or given explicitly) so that the
 * T targets N + 210*i are coprime to M and as many as possible of the
 * intermediate numbers N + j are divisible by a prime of M.
 *
 * For every configuration x we sieve k in [klo, khi) with all primes q <= B
 * not dividing M (T residue classes per q), then Fermat-test the targets of
 * survivors, then check that the remaining intermediates are composite.
 *
 * Build:  gcc -O3 -march=native -o search search.c -lgmp -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <gmp.h>
#include <errno.h>
#include <limits.h>

#define MAXT 10
#define MAXSP 64
#define NPOS 2000

static void fail(const char *message) { fprintf(stderr, "%s\n", message); exit(1); }
static void *checked_calloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) fail("allocation size overflow");
    void *p = calloc(n ? n : 1, size);
    if (!p) fail("out of memory; search stopped without resetting progress");
    return p;
}
static void *checked_realloc(void *old, size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) fail("allocation size overflow");
    void *p = realloc(old, (n ? n : 1) * size);
    if (!p) fail("out of memory; search stopped without resetting progress");
    return p;
}
static uint64_t number(const char *s, uint64_t maximum) {
    char *end; errno = 0;
    if (!s[0] || s[0] < '0' || s[0] > '9') fail("expected a nonnegative integer");
    unsigned long long n = strtoull(s, &end, 10);
    if (errno || *end || n > maximum) fail("integer argument out of range");
    return (uint64_t)n;
}
static uint64_t add64(uint64_t a, uint64_t b) {
    if (a > UINT64_MAX - b) fail("k range overflow");
    return a + b;
}
static uint64_t mul64(uint64_t a, uint64_t b) {
    if (b && a > UINT64_MAX / b) fail("k range overflow");
    return a * b;
}

/* ------------------------------------------------------------------ */
/* small utilities                                                     */
static uint64_t rng_s[2];
static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
static uint64_t rnd(void) {
    uint64_t s0 = rng_s[0], s1 = rng_s[1], r = s0 + s1;
    s1 ^= s0; rng_s[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); rng_s[1] = rotl(s1, 36);
    return r;
}
static void rng_seed(uint64_t seed) {
    rng_s[0] = seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL;
    rng_s[1] = (seed ^ 0xD1B54A32D192ED03ULL) * 0xBF58476D1CE4E5B9ULL + 1;
    for (int i = 0; i < 20; i++) rnd();
}
static double rndf(void) { return (rnd() >> 11) * (1.0 / 9007199254740992.0); }
static int gcd_i(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }
static double now_sec(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

static uint32_t *small_primes; static int n_small_primes;
static void gen_primes(uint32_t limit) {
    uint8_t *s = checked_calloc((size_t)limit + 1, 1);
    small_primes = checked_calloc(limit / 2 + 10, sizeof(uint32_t)); n_small_primes = 0;
    for (uint32_t i = 2; i <= limit; i++) {
        if (s[i]) continue;
        small_primes[n_small_primes++] = i;
        for (uint64_t j = (uint64_t)i * i; j <= limit; j += i) s[j] = 1;
    }
    free(s);
}
static inline uint64_t mulmod64(uint64_t a, uint64_t b, uint64_t m) { return (unsigned __int128)a * b % m; }
static uint64_t invmod64(uint64_t a, uint64_t m) { /* m prime */
    int64_t t = 0, nt = 1; int64_t r = m, nr = a % m;
    while (nr) { int64_t q = r / nr, tmp; tmp = t - q * nt; t = nt; nt = tmp; tmp = r - q * nr; r = nr; nr = tmp; }
    if (t < 0) t += m;
    return t;
}

/* ------------------------------------------------------------------ */
/* problem parameters                                                  */
static int T = 9;                 /* number of targets */
static int SPAN;                  /* 210*(T-1) */
static int sp[MAXSP], nsp;        /* modulus primes >= 11 */
static mpz_t M;

/* ------------------------------------------------------------------ */
/* covering optimizer (simulated annealing)                            */
static int hard[NPOS], nhard;
static int allowed[MAXSP][512], nallowed[MAXSP];
static int *cls[MAXSP][512], clsn[MAXSP][512];
static int clsbuf[MAXSP * NPOS];
static int cnt[NPOS], curb[MAXSP];
/* Upper bound on the positions at most d future swaps can newly cover.
 * Ignoring lost coverage and overlaps makes this conservative. */
static int gain_bound[MAXSP + 1][4];

static void sa_setup_r(int r) {
    nhard = 0;
    for (int j = 1; j < SPAN; j++) {
        if (j % 210 == 0) continue;
        if (gcd_i(r + j, 210) != 1) continue;
        hard[nhard++] = j;
    }
    int *bp = clsbuf;
    for (int pi = 0; pi < nsp; pi++) {
        int p = sp[pi];
        for (int b = 0; b < p; b++) {
            cls[pi][b] = bp; clsn[pi][b] = 0;
            for (int h = 0; h < nhard; h++) if (hard[h] % p == b) { *bp++ = h; clsn[pi][b]++; }
        }
        nallowed[pi] = 0;
        for (int b = 0; b < p; b++) {
            int bad = 0;
            for (int i = 0; i < T; i++) if ((210 * i) % p == b) { bad = 1; break; }
            if (!bad) allowed[pi][nallowed[pi]++] = b;
        }
    }
    memset(gain_bound, 0, sizeof gain_bound);
    for (int pi = nsp - 1; pi >= 0; pi--) {
        int gain = 0;
        for (int ai = 0; ai < nallowed[pi]; ai++) {
            int size = clsn[pi][allowed[pi][ai]];
            if (size > gain) gain = size;
        }
        for (int d = 1; d <= 3; d++) {
            int take = gain + gain_bound[pi + 1][d - 1];
            int skip = gain_bound[pi + 1][d];
            gain_bound[pi][d] = take > skip ? take : skip;
        }
    }
}
static int sa_eval(void) {
    memset(cnt, 0, sizeof(int) * nhard);
    for (int pi = 0; pi < nsp; pi++) { int *c = cls[pi][curb[pi]]; for (int t = 0; t < clsn[pi][curb[pi]]; t++) cnt[c[t]]++; }
    int u = 0; for (int h = 0; h < nhard; h++) if (cnt[h] == 0) u++;
    return u;
}
/* returns best uncovered count; fills bestb[] */
static int sa_run(int r, long iters, int *bestb) {
    sa_setup_r(r);
    for (int pi = 0; pi < nsp; pi++) curb[pi] = allowed[pi][rnd() % nallowed[pi]];
    int unc = sa_eval(), best = unc;
    memcpy(bestb, curb, sizeof(int) * nsp);
    double T0 = 2.0, T1 = 0.05, lr = log(T1 / T0);
    for (long it = 0; it < iters; it++) {
        double Tm = T0 * exp(lr * (double)it / iters);
        int pi = rnd() % nsp;
        int nb = allowed[pi][rnd() % nallowed[pi]], ob = curb[pi];
        if (nb == ob) continue;
        int delta = 0, *c = cls[pi][ob];
        for (int t = 0; t < clsn[pi][ob]; t++) if (cnt[c[t]] == 1) delta++;
        c = cls[pi][nb];
        for (int t = 0; t < clsn[pi][nb]; t++) if (cnt[c[t]] == 0) delta--;
        if (delta <= 0 || rndf() < exp(-delta / Tm)) {
            c = cls[pi][ob]; for (int t = 0; t < clsn[pi][ob]; t++) cnt[c[t]]--;
            c = cls[pi][nb]; for (int t = 0; t < clsn[pi][nb]; t++) cnt[c[t]]++;
            curb[pi] = nb; unc += delta;
            if (unc < best) { best = unc; memcpy(bestb, curb, sizeof(int) * nsp); }
        }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* sieve                                                               */
static uint32_t B = 1u << 18;     /* sieve prime bound */
static uint32_t L = 1u << 18;     /* segment size in bits */
static uint32_t *sq; static int nsq;          /* sieve primes */
static uint32_t *sq_minv, *sq_delta, *sq_nxt; /* per prime: M^-1 mod q, step, next offsets [nsq*T] */

static void sieve_init(void) {
    gen_primes(B);
    sq = checked_calloc(n_small_primes, sizeof(uint32_t));
    sq_minv = checked_calloc(n_small_primes, sizeof(uint32_t));
    sq_delta = checked_calloc(n_small_primes, sizeof(uint32_t));
    nsq = 0;
    for (int i = 0; i < n_small_primes; i++) {
        uint32_t q = small_primes[i];
        if (q <= 7) continue;
        if (mpz_divisible_ui_p(M, q)) continue;
        uint64_t Mq = mpz_fdiv_ui(M, q);
        uint64_t minv = invmod64(Mq, q);
        sq[nsq] = q; sq_minv[nsq] = (uint32_t)minv;
        sq_delta[nsq] = (uint32_t)mulmod64((q - 210 % q) % q, minv, q); /* s_{i+1} = s_i + delta */
        nsq++;
    }
    sq_nxt = checked_calloc((size_t)nsq * T, sizeof(uint32_t));
}

/* statistics */
static uint64_t st_k = 0, st_surv = 0, st_prp[MAXT + 1], st_nearmiss = 0, st_hits = 0, st_cfg = 0, st_var = 0, st_rep = 0;
static double st_t0;

static int fermat2(mpz_t n, mpz_t tmp, mpz_t e) { /* 1 if 2^(n-1) == 1 mod n */
    mpz_sub_ui(e, n, 1);
    mpz_set_ui(tmp, 2);
    mpz_powm(tmp, tmp, e, n);
    return mpz_cmp_ui(tmp, 1) == 0;
}
static int intermediate_prp(mpz_t n, mpz_t tmp, mpz_t e) {
    /* A base-2 pseudoprime is composite and must not reject a CPAP. */
    return fermat2(n, tmp, e) && mpz_probab_prime_p(n, 30) != 0;
}

static FILE *hitf;
static char cfg_desc[4096];

static void check_candidate(uint64_t k, mpz_t x, int *unc, int nunc) {
    static mpz_t N, Ni, tmp, e, g; static int init = 0;
    if (!init) { mpz_inits(N, Ni, tmp, e, g, NULL); init = 1; }
    mpz_mul_ui(N, M, k); mpz_add(N, N, x);
    int np = 0;
    for (int i = 0; i < T; i++) {
        mpz_add_ui(Ni, N, 210 * i);
        if (!fermat2(Ni, tmp, e)) break;
        np++; st_prp[np]++;
    }
    if (np < T) return;
    /* all targets PRP: check uncovered intermediates */
    for (int u = 0; u < nunc; u++) {
        mpz_add_ui(Ni, N, unc[u]);
        if (intermediate_prp(Ni, tmp, e)) {
            st_nearmiss++;
            printf("NEARMISS k=%llu prime intermediate at j=%d  %s\n", (unsigned long long)k, unc[u], cfg_desc);
            fflush(stdout);
            return;
        }
    }
    /* full verification of every intermediate */
    for (int j = 1; j < SPAN; j++) {
        if (j % 210 == 0) continue;
        mpz_add_ui(Ni, N, j);
        mpz_gcd(g, Ni, M);
        if (mpz_cmp_ui(g, 1) != 0) continue;
        if (intermediate_prp(Ni, tmp, e)) {
            printf("BUG: uncovered prime intermediate j=%d not in unc list\n", j); fflush(stdout); return;
        }
    }
    /* stronger PRP test on targets */
    for (int i = 0; i < T; i++) {
        mpz_add_ui(Ni, N, 210 * i);
        if (!mpz_probab_prime_p(Ni, 30)) { printf("Fermat pseudoprime at target %d?!\n", i); fflush(stdout); return; }
    }
    st_hits++;
    char *ns = mpz_get_str(NULL, 10, N);
    char *xs = mpz_get_str(NULL, 10, x);
    printf("HIT CPAP-%d k=%llu digits=%zu N=%s x=%s %s\n", T, (unsigned long long)k, strlen(ns), ns, xs, cfg_desc);
    fflush(stdout);
    if (hitf) { fprintf(hitf, "HIT CPAP-%d k=%llu digits=%zu N=%s x=%s %s\n", T, (unsigned long long)k, strlen(ns), ns, xs, cfg_desc); fflush(hitf); }
    free(ns); free(xs);
}

static void search_config(mpz_t x, uint64_t klo, uint64_t khi, int *unc, int nunc) {
    uint64_t *seg = aligned_alloc(64, L / 8);
    if (!seg) fail("cannot allocate sieve segment");
    /* A target equal to a sieving prime is prime, not a multiple to discard. */
    mpz_t first; mpz_init(first); mpz_mul_ui(first, M, klo); mpz_add(first, first, x);
    while (klo < khi && mpz_cmp_ui(first, B) <= 0) {
        st_surv++; st_k++; check_candidate(klo++, x, unc, nunc); mpz_add(first, first, M);
    }
    mpz_clear(first);
    /* per prime: first offsets relative to klo */
    for (int i = 0; i < nsq; i++) {
        uint32_t q = sq[i];
        uint64_t xq = mpz_fdiv_ui(x, q);
        uint64_t s0 = mulmod64((q - xq) % q, sq_minv[i], q);   /* k == s0 (mod q) => q | k*M + x */
        uint64_t kq = klo % q;
        uint32_t *nx = sq_nxt + (size_t)i * T;
        uint64_t s = s0;
        for (int t = 0; t < T; t++) {
            nx[t] = (uint32_t)((s + q - kq) % q);
            s += sq_delta[i]; if (s >= q) s -= q;
        }
    }
    uint64_t words = L / 64;
    for (uint64_t base = klo; base < khi;) {
        memset(seg, 0, L / 8);
        uint64_t seglen = (khi - base < L) ? (khi - base) : L;
        for (int i = 0; i < nsq; i++) {
            uint32_t q = sq[i];
            uint32_t *nx = sq_nxt + (size_t)i * T;
            for (int t = 0; t < T; t++) {
                uint32_t o = nx[t];
                while (o < L) { seg[o >> 6] |= 1ULL << (o & 63); o += q; }
                nx[t] = o - L;
            }
        }
        if (seglen < L) { /* mask tail */
            for (uint64_t o = seglen; o < L; o++) seg[o >> 6] |= 1ULL << (o & 63);
        }
        for (uint64_t w = 0; w < words; w++) {
            uint64_t v = ~seg[w];
            while (v) {
                int b = __builtin_ctzll(v); v &= v - 1;
                uint64_t k = base + w * 64 + b;
                st_surv++;
                check_candidate(k, x, unc, nunc);
            }
        }
        st_k += seglen;
        base += seglen; /* Avoid overflow on a final partial segment. */
    }
    free(seg);
}

/* compute x from r and residues b[] by CRT; also fill unc[] */
static void build_x(int r, int *b, mpz_t x, int *unc, int *nunc) {
    mpz_t mod, t, inv, g; mpz_inits(mod, t, inv, g, NULL);
    mpz_set_ui(x, r); mpz_set_ui(mod, 210);
    for (int pi = 0; pi < nsp; pi++) {
        int p = sp[pi];
        /* want x + mod*t == -b (mod p) */
        long xp = mpz_fdiv_ui(x, p);
        long target = (p - b[pi]) % p;
        long diff = ((target - xp) % p + p) % p;
        long modp = mpz_fdiv_ui(mod, p);
        long tt = (long)mulmod64(diff, invmod64(modp, p), p);
        mpz_mul_ui(t, mod, tt); mpz_add(x, x, t);
        mpz_mul_ui(mod, mod, p);
    }
    *nunc = 0;
    for (int j = 1; j < SPAN; j++) {
        if (j % 210 == 0) continue;
        mpz_add_ui(t, x, j); mpz_gcd(g, t, M);
        if (mpz_cmp_ui(g, 1) == 0) unc[(*nunc)++] = j;
    }
    /* sanity: targets coprime to M */
    for (int i = 0; i < T; i++) { mpz_add_ui(t, x, 210 * i); mpz_gcd(g, t, M); if (mpz_cmp_ui(g, 1) != 0) { fprintf(stderr, "BUG: target %d not coprime to M\n", i); exit(1); } }
    mpz_clears(mod, t, inv, g, NULL);
}

/* ------------------------------------------------------------------ */
/* per-process registry of searched offsets: x -> number of times seen  */
typedef struct { uint64_t key, count; mpz_t x; } seen_entry;
static seen_entry *htable;
static size_t hused, hsize;
static void seen_grow(void) {
    if (hsize > SIZE_MAX / 2) fail("offset registry capacity overflow");
    size_t size = hsize ? hsize * 2 : 1024;
    seen_entry *next = checked_calloc(size, sizeof *next);
    for (size_t i = 0; i < hsize; i++) if (htable[i].count) {
        size_t h = (size_t)htable[i].key & (size - 1);
        while (next[h].count) h = (h + 1) & (size - 1);
        next[h] = htable[i]; /* Transfer ownership of the GMP limbs. */
    }
    free(htable); htable = next; hsize = size;
}
static uint64_t seen_with_key(mpz_srcptr x, uint64_t key) {
    if (!hsize) seen_grow();
    size_t h = (size_t)key & (hsize - 1);
    while (htable[h].count) {
        if (htable[h].key == key && mpz_cmp(htable[h].x, x) == 0) {
            if (htable[h].count == UINT64_MAX) fail("offset repeat count overflow");
            return htable[h].count++;
        }
        h = (h + 1) & (hsize - 1);
    }
    if (hused >= hsize - hsize / 4) {
        seen_grow(); h = (size_t)key & (hsize - 1);
        while (htable[h].count) h = (h + 1) & (hsize - 1);
    }
    htable[h].key = key; htable[h].count = 1; mpz_init_set(htable[h].x, x); hused++;
    return 0;
}
static uint64_t seen_count(mpz_srcptr x) {
    uint64_t key = mpz_get_ui(x) ^ ((uint64_t)mpz_fdiv_ui(x, 4294967291u) << 32) ^ ((uint64_t)mpz_fdiv_ui(x, 2147483647u) << 17);
    key ^= key >> 30; key *= 0xBF58476D1CE4E5B9ULL; key ^= key >> 27;
    return seen_with_key(x, key);
}
static uint64_t cfg_key(int r, int *b) {   /* hash of the residue vector (determines x) */
    uint64_t h = 1469598103934665603ULL ^ (uint64_t)r;
    for (int pi = 0; pi < nsp; pi++) { h ^= (uint64_t)(b[pi] + 1) * 0x9E3779B97F4A7C15ULL; h *= 1099511628211ULL; h ^= h >> 29; }
    return h;
}

static void print_stats(FILE *f);

/* ------------------------------------------------------------------ */
/* bases mode: deterministic enumeration of swap-variants of shared base
 * coverings (file src/bases_<P>.txt); the variant list is deduplicated,
 * sorted by quality and then searched in k-layers [L*KX+1,(L+1)*KX+1),
 * striped over processes (variant index mod GNUM == g).                 */
/* Keep millions of variants at 12 bytes each. A swap packs a 6-bit prime
 * index and a 9-bit residue; the upper two bits of quality store its depth. */
typedef struct { uint32_t base; uint16_t sw[3], quality; } variant_t;
static int variant_unc(const variant_t *v) { return v->quality & 0x3fff; }
static int variant_depth(const variant_t *v) { return v->quality >> 14; }
static int swap_prime(uint16_t sw) { return sw >> 9; }
static int swap_residue(uint16_t sw) { return sw & 511; }
typedef struct { uint64_t key; uint32_t idx; } vkey_t;
static variant_t *vlist; static vkey_t *vkeys; static size_t vn = 0, vcap = 0;
static int bm_smax = 2, bm_maxunc;
static long bm_layer0 = 0, bm_nlayers = 1000000;
static size_t bm_maxvar = 8000000;
static uint16_t cur_sw[3][2];

static void bm_record(int r, int u, int depth, uint32_t base) {
    if (vn >= bm_maxvar) return;
    if (vn == vcap) { vcap = vcap ? vcap * 2 : 1 << 16; vlist = checked_realloc(vlist, vcap, sizeof(variant_t)); vkeys = checked_realloc(vkeys, vcap, sizeof(vkey_t)); }
    variant_t *v = &vlist[vn]; v->base = base; v->quality = (uint16_t)(u | (depth << 14));
    for (int i = 0; i < 3; i++) v->sw[i] = i < depth ? (uint16_t)((cur_sw[i][0] << 9) | cur_sw[i][1]) : 0;
    vkeys[vn].key = cfg_key(r, curb); vkeys[vn].idx = (uint32_t)vn;
    vn++;
}
static size_t bm_quota = 30000, bm_base_count;
static int bm_target_depth;
/* fixed-depth DFS: records only variants with exactly bm_target_depth swaps */
static void bm_enumerate(int r, int u, int depth, int start_pi, uint32_t base) {
    if (vn >= bm_maxvar || bm_base_count >= bm_quota) return;
    for (int pi = start_pi; pi <= nsp - (bm_target_depth - depth); pi++) {
        int ob = curb[pi], ones = 0, *cl = cls[pi][ob];
        for (int t = 0; t < clsn[pi][ob]; t++) if (cnt[cl[t]] == 1) ones++;
        for (int ai = 0; ai < nallowed[pi]; ai++) {
            int nb = allowed[pi][ai];
            if (nb == ob) continue;
            int zeros = 0; int *cn = cls[pi][nb];
            for (int t = 0; t < clsn[pi][nb]; t++) if (cnt[cn[t]] == 0) zeros++;
            int u2 = u + ones - zeros;
            int remaining = bm_target_depth - depth - 1;
            if (u2 - gain_bound[pi + 1][remaining] > bm_maxunc) continue;
            for (int t = 0; t < clsn[pi][ob]; t++) cnt[cl[t]]--;
            for (int t = 0; t < clsn[pi][nb]; t++) cnt[cn[t]]++;
            curb[pi] = nb;
            if (depth < 3) { cur_sw[depth][0] = pi; cur_sw[depth][1] = nb; }
            if (depth + 1 == bm_target_depth) { bm_record(r, u2, depth + 1, base); bm_base_count++; }
            else bm_enumerate(r, u2, depth + 1, pi + 1, base);
            curb[pi] = ob;
            for (int t = 0; t < clsn[pi][nb]; t++) cnt[cn[t]]--;
            for (int t = 0; t < clsn[pi][ob]; t++) cnt[cl[t]]++;
            if (vn >= bm_maxvar || bm_base_count >= bm_quota) return;
        }
    }
}
static int cmp_key(const void *a, const void *b) {
    const vkey_t *x = a, *y = b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return x->idx < y->idx ? -1 : (x->idx > y->idx);
}
static int cmp_qual(const void *a, const void *b) {
    const vkey_t *x = a, *y = b;   /* key field reused as (u<<32 | idx) */
    return x->key < y->key ? -1 : (x->key > y->key);
}
static int same_variant(const variant_t *a, const variant_t *b, int (*bases)[MAXSP + 2]) {
    if (bases[a->base][0] != bases[b->base][0]) return 0;
    int as = 0, bs = 0;
    for (int pi = 0; pi < nsp; pi++) {
        int av = bases[a->base][pi + 2], bv = bases[b->base][pi + 2];
        if (as < variant_depth(a) && swap_prime(a->sw[as]) == pi) av = swap_residue(a->sw[as++]);
        if (bs < variant_depth(b) && swap_prime(b->sw[bs]) == pi) bv = swap_residue(b->sw[bs++]);
        if (av != bv) return 0;
    }
    return 1;
}
static size_t deduplicate_variants(int (*bases)[MAXSP + 2]) {
    qsort(vkeys, vn, sizeof(vkey_t), cmp_key);
    size_t m = 0;
    for (size_t start = 0; start < vn;) {
        size_t end = start + 1, kept_start = m;
        while (end < vn && vkeys[end].key == vkeys[start].key) end++;
        for (size_t i = start; i < end; i++) {
            int duplicate = 0;
            for (size_t j = kept_start; j < m; j++)
                if (same_variant(&vlist[vkeys[i].idx], &vlist[vkeys[j].idx], bases)) { duplicate = 1; break; }
            if (!duplicate) vkeys[m++] = vkeys[i];
        }
        start = end;
    }
    return m;
}
static int valid_residues(int r, const int *b) {
    if (r < 1 || r >= 210 || gcd_i(r, 210) != 1) return 0;
    for (int pi = 0; pi < nsp; pi++) {
        if (b[pi] < 0 || b[pi] >= sp[pi]) return 0;
        for (int i = 0; i < T; i++) if ((210 * i) % sp[pi] == b[pi]) return 0;
    }
    return 1;
}
static uint64_t header_number(const char *p, uint64_t maximum) {
    char value[32]; size_t n = strcspn(p, " \t\r\n");
    if (n >= sizeof value) fail("bases header integer too long");
    memcpy(value, p, n); value[n] = 0;
    return number(value, maximum);
}

/* returns 1 if bases mode ran */
static int run_bases_mode(int P, int maxunc, long gidx, long gnum, uint64_t klo, uint64_t khi) {
    char fn[256]; snprintf(fn, sizeof fn, "src/bases_%d.txt", P);
    FILE *f = fopen(fn, "r"); if (!f) return 0;
    char line[8192]; int nb = 0; int (*bases)[MAXSP + 2] = checked_calloc(200000, sizeof *bases);
    bm_smax = 2;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            char *p;
            if ((p = strstr(line, "SWAPS="))) bm_smax = (int)header_number(p + 6, 3);
            if ((p = strstr(line, "LAYER0="))) bm_layer0 = (long)header_number(p + 7, LONG_MAX);
            if ((p = strstr(line, "LAYERS="))) bm_nlayers = (long)header_number(p + 7, LONG_MAX);
            if ((p = strstr(line, "MAXVAR="))) bm_maxvar = (size_t)header_number(p + 7, UINT32_MAX);
            if ((p = strstr(line, "QUOTA="))) bm_quota = (size_t)header_number(p + 6, UINT32_MAX);
            if ((p = strstr(line, "REMAP="))) {
                /* REMAP=suffix:wid,... : processes whose git branch ends with suffix get worker id wid (4 procs/worker) */
                char head[512] = "";
                const char *branch = getenv("CPAP_WORKER_BRANCH");
                if (branch) snprintf(head, sizeof head, "%s", branch);
                else { FILE *hf = fopen(".git/HEAD", "r");
                    if (hf) { if (!fgets(head, sizeof head, hf)) head[0] = 0; fclose(hf); }
                }
                char *q = p + 6; char tokbuf[1024]; strncpy(tokbuf, q, sizeof tokbuf - 1); tokbuf[sizeof tokbuf - 1] = 0;
                char *tk = strtok(tokbuf, ", \n");
                while (tk) {
                    char *colon = strchr(tk, ':');
                    if (colon) { *colon = 0; long wid = (long)number(colon + 1, (LONG_MAX - 3) / 4); size_t sl = strlen(tk);
                        size_t hl = strlen(head); while (hl && (head[hl-1] == '\n' || head[hl-1] == '\r')) head[--hl] = 0;
                        if (sl && hl >= sl && strcmp(head + hl - sl, tk) == 0 && gidx >= 0) { long c = gidx % 4; gidx = wid * 4 + c; printf("REMAP: branch %s -> worker %ld, process index %ld\n", head, wid, gidx); }
                    }
                    tk = strtok(NULL, ", \n");
                }
            }
            continue;
        }
        if (strncmp(line, "CFG", 3)) continue;
        char *p = strstr(line, "unc="); if (!p) continue; int u = atoi(p + 4);
        p = strstr(line, "r="); if (!p) continue; int r = atoi(p + 2);
        int b[MAXSP]; for (int pi = 0; pi < nsp; pi++) b[pi] = -1;
        char *tok = strtok(line, " \n");
        while (tok) { int pp, bb; if (sscanf(tok, "%d:%d", &pp, &bb) == 2) { for (int pi = 0; pi < nsp; pi++) if (sp[pi] == pp) b[pi] = bb; } tok = strtok(NULL, " \n"); }
        int ok = 1; for (int pi = 0; pi < nsp; pi++) if (b[pi] < 0) ok = 0;
        if (!ok || !valid_residues(r, b)) fail("invalid base residues");
        if (nb >= 200000) fail("too many base configurations");
        bases[nb][0] = r; bases[nb][1] = u; memcpy(&bases[nb][2], b, sizeof(int) * nsp); nb++;
    }
    fclose(f);
    if ((gnum > 0 && (gidx < 0 || gidx >= gnum)) || bm_nlayers > LONG_MAX - bm_layer0)
        fail("invalid bases process index or layer range");
    if (bm_smax <= 0 || nb == 0) { free(bases); return 0; }
    if (bm_smax > 3) bm_smax = 3;
    bm_maxunc = maxunc;
    uint64_t KX = khi - klo;
    printf("bases mode: %d bases from %s, swaps<=%d, maxunc=%d, maxvar=%zu, quota=%zu, layers from %ld, KX=%llu, process %ld of %ld\n", nb, fn, bm_smax, maxunc, bm_maxvar, bm_quota, bm_layer0, (unsigned long long)KX, gidx, gnum);
    fflush(stdout);
    double t0 = now_sec();
    for (int i = 0; i < nb && vn < bm_maxvar; i++) {
        int r = bases[i][0];
        sa_setup_r(r);
        memcpy(curb, &bases[i][2], sizeof(int) * nsp);
        int u = sa_eval();
        if (u != bases[i][1]) fprintf(stderr, "warning: base %d unc %d != file %d\n", i, u, bases[i][1]);
        bm_base_count = 0;
        if (u <= bm_maxunc) bm_record(r, u, 0, (uint32_t)i);
        for (bm_target_depth = 1; bm_target_depth <= bm_smax && bm_base_count < bm_quota; bm_target_depth++)
            bm_enumerate(r, u, 0, 0, (uint32_t)i);
    }
    /* Compare full vectors within hash groups, keeping the first occurrence. */
    size_t m = deduplicate_variants(bases);
    size_t ndup = vn - m;
    for (size_t i = 0; i < m; i++) vkeys[i].key = ((uint64_t)variant_unc(&vlist[vkeys[i].idx]) << 32) | vkeys[i].idx;
    qsort(vkeys, m, sizeof(vkey_t), cmp_qual);
    variant_t *sorted = checked_calloc(m, sizeof(variant_t));
    for (size_t i = 0; i < m; i++) sorted[i] = vlist[vkeys[i].idx];
    free(vlist); vlist = sorted; vn = m; free(vkeys); vkeys = NULL;
    {
        double su = 0; long hist[8] = {0};
        for (size_t i = 0; i < vn; i++) { int u = variant_unc(&vlist[i]); su += u; int d = bm_maxunc - u; if (d > 7) d = 7; hist[d]++; }
        printf("variant list: %zu variants (%zu duplicates removed) in %.0fs, avg unc %.2f; counts by unc:", vn, ndup, now_sec() - t0, vn ? su / vn : 0);
        for (int d = 0; d < 8; d++) if (hist[d]) printf(" %d:%ld", bm_maxunc - d, hist[d]);
        printf("\n"); fflush(stdout);
    }
    mpz_t x; mpz_init(x); int unc[NPOS], nunc, b2[MAXSP];
    for (long L = bm_layer0; L < bm_layer0 + bm_nlayers; L++) {
        uint64_t lk = add64(klo, mul64((uint64_t)L, KX)), hk = add64(lk, KX);
        printf("LAYER %ld k=[%llu,%llu) start (searched so far: cfgs=%llu k=%llu)\n", L, (unsigned long long)lk, (unsigned long long)hk, (unsigned long long)st_cfg, (unsigned long long)st_k); fflush(stdout);
        for (size_t i = 0; i < vn; i++) {
            if (gnum > 0 && (long)(i % gnum) != gidx) continue;
            variant_t *v = &vlist[i];
            int r = bases[v->base][0];
            memcpy(b2, &bases[v->base][2], sizeof(int) * nsp);
            for (int s2 = 0; s2 < variant_depth(v); s2++) b2[swap_prime(v->sw[s2])] = swap_residue(v->sw[s2]);
            build_x(r, b2, x, unc, &nunc);
            if (nunc != variant_unc(v)) { fprintf(stderr, "BUG: variant unc mismatch %d vs %d\n", nunc, variant_unc(v)); exit(1); }
            snprintf(cfg_desc, sizeof cfg_desc, "bases layer=%ld idx=%zu base#%u swaps=%d unc=%d r=%d k=[%llu,%llu)", L, i, v->base, variant_depth(v), nunc, r, (unsigned long long)lk, (unsigned long long)hk);
            st_cfg++;
            search_config(x, lk, hk, unc, nunc);
            if (st_cfg % 20 == 0) print_stats(stdout);
        }
    }
    print_stats(stdout);
    free(bases);
    return 1;
}

static void print_stats(FILE *f) {
    double el = now_sec() - st_t0;
    fprintf(f, "STAT cfgs=%llu vars=%llu reps=%llu k=%llu surv=%llu", (unsigned long long)st_cfg, (unsigned long long)st_var, (unsigned long long)st_rep, (unsigned long long)st_k, (unsigned long long)st_surv);
    for (int i = 1; i <= T; i++) fprintf(f, " prp%d=%llu", i, (unsigned long long)st_prp[i]);
    fprintf(f, " nearmiss=%llu hits=%llu elapsed=%.0fs rate=%.3g k/s\n", (unsigned long long)st_nearmiss, (unsigned long long)st_hits, el, st_k / (el > 0 ? el : 1));
    fflush(f);
}

int main(int argc, char **argv) {
    int P = 127, excl[64], nexcl = 0;
    uint64_t klo = 1, khi = 100000000ULL;
    const char *xstr = NULL, *cfgfile = NULL, *hitfile = NULL;
    uint64_t seed = 1; long nconfigs = 1000000000L, saiters = 1000000; int maxunc = 100000, statevery = 20, maxvar = 0, baseunc = -1;
    long gidx = -1, gnum = 0; uint64_t KX = 0;
    for (int a = 1; a < argc; a++) {
        if (a + 1 >= argc) fail("option requires a value");
        if (!strcmp(argv[a], "-P")) P = (int)number(argv[++a], 511);
        else if (!strcmp(argv[a], "-X")) { char *s = strdup(argv[++a]); if (!s) fail("out of memory"); char *tok = strtok(s, ","); while (tok) { if (nexcl >= MAXSP) fail("too many excluded primes"); excl[nexcl++] = (int)number(tok, 511); tok = strtok(NULL, ","); } free(s); }
        else if (!strcmp(argv[a], "-T")) T = (int)number(argv[++a], MAXT);
        else if (!strcmp(argv[a], "-B")) B = (uint32_t)number(argv[++a], UINT32_MAX);
        else if (!strcmp(argv[a], "-L")) L = (uint32_t)number(argv[++a], UINT32_MAX);
        else if (!strcmp(argv[a], "-k")) klo = number(argv[++a], UINT64_MAX);
        else if (!strcmp(argv[a], "-K")) khi = number(argv[++a], UINT64_MAX);
        else if (!strcmp(argv[a], "-x")) xstr = argv[++a];
        else if (!strcmp(argv[a], "-c")) cfgfile = argv[++a];
        else if (!strcmp(argv[a], "-s")) seed = number(argv[++a], UINT64_MAX);
        else if (!strcmp(argv[a], "-n")) nconfigs = (long)number(argv[++a], LONG_MAX);
        else if (!strcmp(argv[a], "-i")) saiters = (long)number(argv[++a], LONG_MAX);
        else if (!strcmp(argv[a], "-u")) maxunc = (int)number(argv[++a], INT_MAX);
        else if (!strcmp(argv[a], "-o")) hitfile = argv[++a];
        else if (!strcmp(argv[a], "-e")) statevery = (int)number(argv[++a], INT_MAX);
        else if (!strcmp(argv[a], "-v")) maxvar = (int)number(argv[++a], INT_MAX);
        else if (!strcmp(argv[a], "-w")) baseunc = (int)number(argv[++a], INT_MAX);
        else if (!strcmp(argv[a], "-g")) gidx = (long)number(argv[++a], LONG_MAX);
        else if (!strcmp(argv[a], "-G")) gnum = (long)number(argv[++a], LONG_MAX);
        else { fprintf(stderr, "unknown arg %s\n", argv[a]); return 1; }
    }
    if (T < 8 || T > MAXT) fail("-T must be 8, 9 or 10");
    if (P < 11 || P >= 512) fail("-P must be between 11 and 511");
    if (L < 512 || L % 512 || B < 2 || B > UINT32_MAX - L) fail("-L must be a positive multiple of 512; require 2 <= B <= UINT32_MAX-L");
    if (klo == 0 || klo >= khi) fail("require 1 <= -k < -K");
    if (!statevery || !saiters) fail("-e and -i must be positive");
    if ((gnum == 0 && gidx != -1) || (gnum > 0 && gidx < 0)) fail("-g and -G must be supplied together");
    if (xstr && cfgfile) fail("choose either -x or -c");
    SPAN = 210 * (T - 1);
    /* modulus */
    nsp = 0; mpz_init_set_ui(M, 210);
    for (int p = 11; p <= P; p++) {
        int ip = 1; for (int d = 2; d * d <= p; d++) if (p % d == 0) ip = 0;
        if (!ip) continue;
        int ex = 0; for (int e = 0; e < nexcl; e++) if (excl[e] == p) ex = 1;
        if (ex) continue;
        if (nsp >= MAXSP) fail("too many modulus primes (maximum 64)");
        sp[nsp++] = p; mpz_mul_ui(M, M, p);
    }
    if (!nsp) fail("at least one modulus prime >= 11 is required");
    {
        char *ms = mpz_get_str(NULL, 10, M);
        printf("CPAP-%d search: M = 210", T);
        for (int i = 0; i < nsp; i++) printf("*%d", sp[i]);
        printf(" (%zu digits) B=%u L=%u k=[%llu,%llu)\n", strlen(ms), B, L, (unsigned long long)klo, (unsigned long long)khi);
        free(ms);
    }
    sieve_init();
    printf("sieve primes: %d (largest %u)\n", nsq, nsq ? sq[nsq - 1] : 0);
    if (hitfile) { hitf = fopen(hitfile, "a"); if (!hitf) { perror(hitfile); return 1; } }
    st_t0 = now_sec();
    rng_seed(seed);
    mpz_t x; mpz_init(x);
    int unc[NPOS], nunc;
    int b[MAXSP];

    if (xstr) {
        if (mpz_set_str(x, xstr, 10) || mpz_sgn(x) < 0) fail("-x must be a nonnegative decimal integer");
        mpz_t t, g; mpz_inits(t, g, NULL);
        nunc = 0;
        for (int j = 1; j < SPAN; j++) { if (j % 210 == 0) continue; mpz_add_ui(t, x, j); mpz_gcd(g, t, M); if (mpz_cmp_ui(g, 1) == 0) unc[nunc++] = j; }
        for (int i = 0; i < T; i++) { mpz_add_ui(t, x, 210 * i); mpz_gcd(g, t, M); if (mpz_cmp_ui(g, 1) != 0) { fprintf(stderr, "target %d not coprime to M\n", i); return 1; } }
        snprintf(cfg_desc, sizeof cfg_desc, "explicit-x unc=%d", nunc);
        printf("explicit x, uncovered intermediates: %d\n", nunc);
        st_cfg++;
        search_config(x, klo, khi, unc, nunc);
        print_stats(stdout);
        return 0;
    }
    if (cfgfile) {
        FILE *f = fopen(cfgfile, "r"); if (!f) { perror("cfgfile"); return 1; }
        char line[8192];
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, "CFG", 3)) continue;
            int u, r; char *p = strstr(line, "unc="); if (!p) continue; u = atoi(p + 4);
            p = strstr(line, "r="); if (!p) continue; r = atoi(p + 2);
            /* residues "p:b" */
            for (int pi = 0; pi < nsp; pi++) b[pi] = -1;
            char *tok = strtok(line, " \n");
            while (tok) { int pp, bb; if (sscanf(tok, "%d:%d", &pp, &bb) == 2) { for (int pi = 0; pi < nsp; pi++) if (sp[pi] == pp) b[pi] = bb; } tok = strtok(NULL, " \n"); }
            int ok = 1; for (int pi = 0; pi < nsp; pi++) if (b[pi] < 0) ok = 0;
            if (!ok || !valid_residues(r, b)) fail("invalid config residues");
            build_x(r, b, x, unc, &nunc);
            char *xs = mpz_get_str(NULL, 10, x);
            snprintf(cfg_desc, sizeof cfg_desc, "cfg#%llu unc=%d r=%d", (unsigned long long)st_cfg, nunc, r);
            printf("CONFIG %llu x=%s unc=%d (file says %d) r=%d\n", (unsigned long long)st_cfg, xs, nunc, u, r); fflush(stdout);
            free(xs);
            st_cfg++;
            search_config(x, klo, khi, unc, nunc);
            if (st_cfg % statevery == 0) print_stats(stdout);
        }
        print_stats(stdout);
        return 0;
    }
    if (run_bases_mode(P, maxunc, gidx, gnum, klo, khi)) return 0;
    /* generated configs */
    int rs[48], nr = 0;
    for (int r = 1; r < 210; r++) if (gcd_i(r, 210) == 1) rs[nr++] = r;
    int b2[MAXSP];
    if (baseunc < 0) baseunc = maxunc;
    KX = khi - klo;
    if (gnum > 0 && (gidx < 0 || gidx >= gnum)) { fprintf(stderr, "-g must be in [0,-G)\n"); return 1; }
    if (gnum > 0) printf("process %ld of %ld: k ranges [(%ld + n*%ld)*%llu + %llu, ...) per offset occurrence n\n", gidx, gnum, gidx, gnum, (unsigned long long)KX, (unsigned long long)klo);
    for (long c = 0; c < nconfigs; c++) {
        int r, u, tries = 0;
        do {
            r = rs[rnd() % nr];
            u = sa_run(r, saiters, b);
            tries++;
        } while (u > baseunc && tries < 50);
        if (u > baseunc) { fprintf(stderr, "could not reach baseunc=%d (best %d)\n", baseunc, u); continue; }
        build_x(r, b, x, unc, &nunc);
        if (nunc != u) { fprintf(stderr, "BUG: unc mismatch %d vs %d\n", nunc, u); return 1; }
        char *xs = mpz_get_str(NULL, 10, x);
        uint64_t rlo = klo, rhi = khi, rep = 0;
        if (gnum > 0) { rep = seen_count(x); if (rep) st_rep++; rlo = add64(klo, mul64(add64(gidx, mul64(rep, gnum)), KX)); rhi = add64(rlo, KX); }
        snprintf(cfg_desc, sizeof cfg_desc, "seed=%llu cfg#%ld unc=%d r=%d rep=%llu k=[%llu,%llu)", (unsigned long long)seed, c, nunc, r, (unsigned long long)rep, (unsigned long long)rlo, (unsigned long long)rhi);
        printf("CONFIG %ld x=%s unc=%d r=%d tries=%d rep=%llu klo=%llu\n", c, xs, nunc, r, tries, (unsigned long long)rep, (unsigned long long)rlo); fflush(stdout);
        free(xs);
        st_cfg++;
        search_config(x, rlo, rhi, unc, nunc);
        if (st_cfg % statevery == 0) print_stats(stdout);
        if (maxvar <= 0) continue;
        /* single-residue variants of this covering with at most maxunc uncovered */
        memcpy(curb, b, sizeof(int) * nsp);
        sa_eval();   /* cnt[] now describes the base covering (sa_setup_r(r) still active) */
        int nv = 0;
        for (int pi = 0; pi < nsp && nv < maxvar; pi++) {
            int ob = curb[pi], ones = 0, *cl = cls[pi][ob];
            for (int t = 0; t < clsn[pi][ob]; t++) if (cnt[cl[t]] == 1) ones++;
            for (int ai = 0; ai < nallowed[pi] && nv < maxvar; ai++) {
                int nb = allowed[pi][ai];
                if (nb == ob) continue;
                int zeros = 0; cl = cls[pi][nb];
                for (int t = 0; t < clsn[pi][nb]; t++) if (cnt[cl[t]] == 0) zeros++;
                if (u + ones - zeros > maxunc) continue;
                memcpy(b2, b, sizeof(int) * nsp); b2[pi] = nb;
                build_x(r, b2, x, unc, &nunc);
                if (nunc != u + ones - zeros) { fprintf(stderr, "BUG: variant unc mismatch %d vs %d\n", nunc, u + ones - zeros); return 1; }
                rlo = klo; rhi = khi; rep = 0;
                if (gnum > 0) { rep = seen_count(x); if (rep) st_rep++; rlo = add64(klo, mul64(add64(gidx, mul64(rep, gnum)), KX)); rhi = add64(rlo, KX); }
                snprintf(cfg_desc, sizeof cfg_desc, "seed=%llu cfg#%ld var%d(%d:%d) unc=%d r=%d rep=%llu k=[%llu,%llu)", (unsigned long long)seed, c, nv, sp[pi], nb, nunc, r, (unsigned long long)rep, (unsigned long long)rlo, (unsigned long long)rhi);
                nv++; st_cfg++; st_var++;
                search_config(x, rlo, rhi, unc, nunc);
                if (st_cfg % statevery == 0) print_stats(stdout);
            }
        }
        printf("VARIANTS cfg#%ld n=%d\n", c, nv); fflush(stdout);
    }
    print_stats(stdout);
    return 0;
}
