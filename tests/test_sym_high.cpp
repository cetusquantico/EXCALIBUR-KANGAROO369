// EXCALIBUR-KANGAROO369 — test_sym_high: regression guard for the Phase 6 fix.
// The Phase 3b symmetric walk could not solve above ~52 bits: walks got trapped in long
// self-intersection loops and the DP table filled with same-herd re-hits. This solves a
// 54-bit instance — where the old walk failed even at 16*sqrt(W) — and checks that the
// trap signature (re-hits vs. trails) stays negligible.
#include "ec.h"
#include "kangaroo.h"
#include "scalar.h"
#include <cstdio>
#include <random>
#include <cmath>

using ec::U256;
static bool eqU(const U256& a, const U256& b) {
    return a.v[0]==b.v[0] && a.v[1]==b.v[1] && a.v[2]==b.v[2] && a.v[3]==b.v[3];
}

int main() {
    ec::init();
    const int range = 54;
    std::mt19937_64 rng(0xABCDEF1234ULL + range);                     // same instance as the diagnosis
    const U256 secret = sc::add(sc::pow2(range - 1), sc::rand_bits(rng, range - 1));
    const ec::Point target = ec::mul_g(secret);

    kang::CkptOpts ck; ck.max_jumps = (uint64_t)(12.0 * std::pow(2.0, range / 2.0));   // safety cap
    const kang::Result r = kang::solve_sym(target, range, 0x1234567, 3, &ck);

    printf("== test_sym_high: %d-bit symmetric solve ==\n", range);
    printf("  found=%d K=%.2f jumps=%llu time=%.1fs dps=%llu rehits=%llu stall-restarts=%llu\n",
           (int)r.found, r.K, (unsigned long long)r.jumps, r.seconds,
           (unsigned long long)r.dp_count, (unsigned long long)r.rehits,
           (unsigned long long)r.restarts);

    const bool key_ok  = r.found && eqU(r.key, secret);
    const bool healthy = r.rehits * 100 <= r.dp_count + 100;           // re-hits <= ~1% of trails
    printf("  %s : key recovered\n", key_ok ? "ok  " : "FAIL");
    printf("  %s : no trap signature (re-hits <= 1%% of DPs)\n", healthy ? "ok  " : "FAIL");
    if (key_ok && healthy) { printf("RESULT: PASS\n"); return 0; }
    printf("RESULT: FAIL\n");
    return 1;
}
