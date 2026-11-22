// Phase 3b test: compare plain kangaroo vs +-P symmetry kangaroo on identical
// synthetic puzzles. Both must recover k; symmetry should lower K (jumps/sqrt).
#include "../src/ec.h"
#include "../src/kangaroo.h"
#include <cstdio>
#include <random>

int main(int argc, char** argv) {
    ec::init();
    std::mt19937_64 rng(0xBEEF9999);
    int ranges[] = {40, 44, 48};
    int nr = 3;
    int single = (argc > 1) ? atoi(argv[1]) : 0;

    int pass = 0, fail = 0;
    printf("== Phase 3b: plain vs symmetry kangaroo ==\n");
    for (int r = 0; r < nr; ++r) {
        int range = single ? single : ranges[r];
        uint64_t N = 1ull << range;
        uint64_t k = (rng() % N); if (k == 0) k = 1;
        ec::Point P = ec::mul_g(ec::U256{{k, 0, 0, 0}});
        printf("\n-- range %d : secret k = 0x%llx --\n", range, (unsigned long long)k);

        kang::Result a = kang::solve(P, range, 0x111 ^ k, 4);
        kang::Result b = kang::solve_sym(P, range, 0x111 ^ k, 4);

        auto eqk = [&](const kang::Result& r){ return r.found && r.key.v[0]==k && r.key.v[1]==0 && r.key.v[2]==0 && r.key.v[3]==0; };
        bool oka = eqk(a);
        bool okb = eqk(b);
        printf("   plain    : %s  K=%6.2f  jumps=%-11llu time=%.2fs\n",
               oka ? "MATCH" : "WRONG", a.K, (unsigned long long)a.jumps, a.seconds);
        printf("   symmetry : %s  K=%6.2f  jumps=%-11llu time=%.2fs",
               okb ? "MATCH" : "WRONG", b.K, (unsigned long long)b.jumps, b.seconds);
        if (oka && okb && b.jumps > 0)
            printf("   -> %.2fx fewer jumps, %.2fx faster\n",
                   (double)a.jumps / b.jumps, a.seconds / b.seconds);
        else printf("\n");

        (oka && okb) ? ++pass : ++fail;
        if (single) break;
    }
    printf("\n== RESULT: %d passed, %d failed ==\n", pass, fail);
    return fail ? 1 : 0;
}
