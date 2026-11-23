// EXCALIBUR-KANGAROO369 — Pollard's Kangaroo solver (CPU, single-thread, Phase 2)
// Solves k from P = k*G, with k in [0, 2^range), using tame/wild herds + DP.
#pragma once
#include "ec.h"
#include <cstdint>
#include <atomic>
#include <string>

namespace kang {

// Phase 5: checkpoint/resume options. Pass nullptr (default) to disable entirely —
// existing callers and tests are unaffected.
struct CkptOpts {
    std::string        path;              // checkpoint file; empty = disabled
    int                interval_sec = 30; // snapshot cadence while running
    bool               resume = false;    // load `path` before starting
    uint64_t           session_salt = 0;  // perturbs start origins (use a new one per resume)
    uint64_t           max_jumps = 0;     // 0 = default cap; else stop after N (tests/interruption)
    std::atomic<bool>* stop = nullptr;    // external stop request (e.g. SIGINT); may be null
    uint64_t           loaded_dps = 0;    // [out] how many DPs were loaded on resume
};

struct Result {
    bool      found = false;
    ec::U256  key{{0,0,0,0}};   // recovered k (valid if found); 256-bit (ranges up to 160)
    uint64_t  jumps = 0;        // total group operations performed
    uint64_t  dp_count = 0;     // distinguished points stored
    double    seconds = 0;
    double    K = 0;            // empirical constant: jumps / sqrt(2^range)
    int       threads = 1;
    // diagnostics (solve_sym): cycle escapes, stall restarts, tame/wild DP matches, same-herd re-hits
    uint64_t  escapes = 0, restarts = 0, collisions = 0, rehits = 0;
};

// Solve the interval DLP. `range` must be 2..160. `seed` makes runs reproducible.
// `threads` worker threads share one distinguished-point table (van Oorschot-Wiener).
Result solve(const ec::Point& target, int range, uint64_t seed = 0x1234567,
             int threads = 4);

// Same, but with the +-P negation-map SYMMETRY. Phase 6: implemented as Gaudry–Schost
// walks (restart after every DP, 2/4-cycle escape, in-batch teleports); measured
// K ~1.2-2.0 at 40-56 bits and reliable at every range (the Phase 3b walk stalled
// above ~52 bits). Interval is centered to [-N/2, N/2).
// Phase 5: pass `ck` to checkpoint the DP table periodically and/or resume from it.
Result solve_sym(const ec::Point& target, int range, uint64_t seed = 0x1234567,
                 int threads = 4, CkptOpts* ck = nullptr);

} // namespace kang
