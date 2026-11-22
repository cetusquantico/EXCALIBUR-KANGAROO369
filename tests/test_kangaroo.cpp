// Phase 2 test: generate synthetic exposed-pubkey puzzles and solve them with the
// kangaroo. This is the "known-answer test" of the solver: pick random k, compute
// P = k*G, forget k, solve, and confirm we recover exactly k.
#include "../src/ec.h"
#include "../src/kangaroo.h"
#include <cstdio>
#include <random>

int main(int argc, char** argv) {
    ec::init();
    std::mt19937_64 rng(0xC0FFEE123);

    int ranges[] = {32, 40, 48};
    int nr = 3;
    // allow overriding to a single range from CLI: test_kangaroo 56
    int single = (argc > 1) ? atoi(argv[1]) : 0;

    int pass = 0, fail = 0;
    printf("== Phase 2: synthetic kangaroo solves (exposed pubkey) ==\n");
    for (int r = 0; r < nr; ++r) {
        int range = single ? single : ranges[r];
        uint64_t N = 1ull << range;
        uint64_t k = (rng() % N);                 // the secret, in [0, 2^range)
        if (k == 0) k = 1;
        ec::Point P = ec::mul_g(ec::U256{{k, 0, 0, 0}});

        printf("\n-- range %d bits : secret k = 0x%llx --\n", range,
               (unsigned long long)k);
        kang::Result res = kang::solve(P, range, 0xABCDEF ^ k);

        bool ok = res.found && res.key.v[0] == k && res.key.v[1] == 0 && res.key.v[2] == 0 && res.key.v[3] == 0;
        printf("   found=%d  recovered=0x%llx  %s\n", res.found,
               (unsigned long long)res.key.v[0], ok ? "MATCH" : "WRONG");
        printf("   jumps=%llu  (K=%.2f * sqrt)  DPs=%llu  threads=%d  time=%.2fs  rate=%.2f M/s\n",
               (unsigned long long)res.jumps, res.K,
               (unsigned long long)res.dp_count, res.threads, res.seconds,
               res.jumps / res.seconds / 1e6);
        ok ? ++pass : ++fail;
        if (single) break;
    }
    printf("\n== RESULT: %d passed, %d failed ==\n", pass, fail);
    return fail ? 1 : 0;
}
