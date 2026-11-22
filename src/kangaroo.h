// EXCALIBUR-KANGAROO369 — Pollard's Kangaroo solver (CPU, single-thread, Phase 2)
// Solves k from P = k*G, with k in [0, 2^range), using tame/wild herds + DP.
#pragma once
#include "ec.h"
#include <cstdint>

namespace kang {

struct Result {
    bool      found = false;
    ec::U256  key{{0,0,0,0}};   // recovered k (valid if found); 256-bit (ranges up to 160)
    uint64_t  jumps = 0;        // total group operations performed
    uint64_t  dp_count = 0;     // distinguished points stored
    double    seconds = 0;
    double    K = 0;            // empirical constant: jumps / sqrt(2^range)
    int       threads = 1;
};

// Solve the interval DLP. `range` must be 2..160. `seed` makes runs reproducible.
// `threads` worker threads share one distinguished-point table (van Oorschot-Wiener).
Result solve(const ec::Point& target, int range, uint64_t seed = 0x1234567,
             int threads = 4);

// Same, but with the +-P negation-map SYMMETRY (Phase 3b): ~1.8x fewer ops (K->~1.15),
// at the cost of fruitless-cycle handling. Interval is centered to [-N/2, N/2).
Result solve_sym(const ec::Point& target, int range, uint64_t seed = 0x1234567,
                 int threads = 4);

} // namespace kang
