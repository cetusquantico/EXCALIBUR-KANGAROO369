// EXCALIBUR-KANGAROO369 — Phase 5: checkpoint/resume of the kangaroo DP table.
//
// What persists is the DISTINGUISHED-POINT table (the shared, accumulated work) plus
// the jump counter and the run config. In-flight kangaroo walks are cheap to recreate,
// so on resume we reload the table and keep adding new DPs (with a fresh per-session salt
// so the walks explore new paths) until a tame/wild collision yields the key.
//
// File layout (little-endian): magic "EXK5", version, range, sym, seed, jumps,
// target(x,y,inf), dp_count, then dp_count * {x(32), type(1), val(32)}, then a trailing
// SHA-256 over everything before it. Written atomically (tmp + fsync + rename), mode 0600.
#pragma once
#include "ec.h"
#include <string>
#include <vector>
#include <cstdint>

namespace kang {

struct DP { ec::U256 x; uint8_t type; ec::U256 val; };

struct Snapshot {
    int       range = 0;
    uint8_t   sym   = 1;
    uint64_t  seed  = 0;        // seed that built the jump table (reused on resume)
    uint64_t  jumps = 0;        // group operations done across all prior sessions
    ec::Point target{};         // the (possibly shifted) point the solver works on
    std::vector<DP> dps;
};

// Atomic write with a trailing SHA-256 integrity digest. Returns false on I/O error.
bool ckpt_save(const std::string& path, const Snapshot& s);

// Load + verify. Returns 0 ok; -1 open/read error; -2 bad magic/version/truncated;
// -3 digest mismatch (corruption/tamper).
int  ckpt_load(const std::string& path, Snapshot& s);

} // namespace kang
