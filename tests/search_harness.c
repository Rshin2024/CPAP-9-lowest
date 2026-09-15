/* Exercise internal search invariants against independent exhaustive cases. */
#define main search_cli_main
#include "../src/search.c"
#undef main
#include <assert.h>

static void reset_variants(void) {
    free(vlist); free(vkeys); vlist = NULL; vkeys = NULL; vn = vcap = 0;
    bm_base_count = 0; bm_maxvar = UINT32_MAX; bm_quota = UINT32_MAX;
}
static void test_pruning(void) {
    int ps[] = {11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101};
    int bs[] = {9,9,5,17,11,17,4,24,2,12,30,19,3,59,8,44,3,10,73,54,17,51};
    T = 9; SPAN = 1680; nsp = 22; memcpy(sp, ps, sizeof ps); sa_setup_r(127);
    memcpy(curb, bs, sizeof bs); assert(sa_eval() == 139);
    curb[2] = 4; assert(sa_eval() == 147); curb[11] = 38; assert(sa_eval() == 145);
    memcpy(curb, bs, sizeof bs); int u = sa_eval();
    reset_variants(); bm_maxunc = 145; bm_target_depth = 2;
    bm_enumerate(127, u, 0, 0, 0);
    int found = 0;
    for (size_t i = 0; i < vn; i++) {
        variant_t *v = &vlist[i];
        if (swap_prime(v->sw[0]) == 2 && swap_residue(v->sw[0]) == 4 &&
            swap_prime(v->sw[1]) == 11 && swap_residue(v->sw[1]) == 38) found++;
    }
    assert(found == 1);
    printf("repairing two-swap variant recovered; %zu variants\n", vn);

    nsp = 3; sa_setup_r(127);
    for (int i = 0; i < nsp; i++) bs[i] = allowed[i][0];
    memcpy(curb, bs, sizeof(int) * nsp); int base_u = sa_eval();
    for (int depth = 2; depth <= 3; depth++) for (int slack = -3; slack <= 3; slack += 3) {
        int expected = 0; bm_maxunc = base_u + slack;
        for (int a = 0; a < nallowed[0]; a++) for (int b = 0; b < nallowed[1]; b++) for (int c = 0; c < nallowed[2]; c++) {
            curb[0] = allowed[0][a]; curb[1] = allowed[1][b]; curb[2] = allowed[2][c];
            int changes = (curb[0] != bs[0]) + (curb[1] != bs[1]) + (curb[2] != bs[2]);
            if (changes == depth && sa_eval() <= bm_maxunc) expected++;
        }
        memcpy(curb, bs, sizeof(int) * nsp); sa_eval(); reset_variants(); bm_target_depth = depth;
        bm_enumerate(127, base_u, 0, 0, 0); assert(vn == (size_t)expected);
    }
    puts("two- and three-swap counts match exhaustive enumeration");
}
static void test_exact_dedup(void) {
    mpz_t x, y; mpz_inits(x, y, NULL); mpz_set_ui(x, 7); mpz_set_ui(y, 8);
    assert(seen_with_key(x, 123) == 0); assert(seen_with_key(y, 123) == 0);
    assert(seen_with_key(x, 123) == 1); assert(seen_with_key(y, 123) == 1);
    size_t initial = hsize;
    for (unsigned long i = 100; i < 10100; i++) { mpz_set_ui(x, i); assert(seen_count(x) == 0); }
    assert(hsize > initial);
    mpz_set_ui(x, 7); assert(seen_with_key(x, 123) == 2); assert(seen_with_key(y, 123) == 2);
    mpz_clears(x, y, NULL);

    reset_variants(); nsp = 2;
    int bases[3][MAXSP + 2] = {{127, 139, 9, 9}, {127, 139, 9, 11}, {127, 139, 9, 9}};
    vn = vcap = 3; vlist = checked_calloc(vn, sizeof *vlist); vkeys = checked_calloc(vn, sizeof *vkeys);
    for (size_t i = 0; i < vn; i++) { vlist[i].base = i; vkeys[i].key = 123; vkeys[i].idx = i; }
    assert(deduplicate_variants(bases) == 2); assert(vkeys[0].idx == 0); assert(vkeys[1].idx == 1);
    puts("registry growth and exact hash-collision handling passed");
}
static void test_pseudoprime_and_packing(void) {
    mpz_t n, tmp, e; mpz_inits(n, tmp, e, NULL); mpz_set_ui(n, 341);
    assert(fermat2(n, tmp, e)); assert(!intermediate_prp(n, tmp, e));
    mpz_set_ui(n, 349); assert(intermediate_prp(n, tmp, e)); mpz_clears(n, tmp, e, NULL);
    reset_variants(); nsp = 1; curb[0] = 500; cur_sw[0][0] = 63; cur_sw[0][1] = 508;
    bm_record(127, 300, 1, 0);
    assert(sizeof(variant_t) == 12); assert(variant_unc(vlist) == 300); assert(variant_depth(vlist) == 1);
    assert(swap_prime(vlist->sw[0]) == 63); assert(swap_residue(vlist->sw[0]) == 508);
    puts("Fermat pseudoprime accepted as composite; packed residues preserve bounds");
}
int main(void) {
    test_pruning(); test_exact_dedup(); test_pseudoprime_and_packing(); reset_variants();
    return 0;
}
