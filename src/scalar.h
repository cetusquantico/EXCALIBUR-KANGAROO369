// EXCALIBUR-KANGAROO369 — plain 256-bit integer ops (two's complement, signed-capable).
// Used for kangaroo distances/exponents so ranges up to 160 bits work without overflow.
// (ec::U256 is the storage; these are *plain integer* ops, NOT the field mod-p ops in ec.)
#pragma once
#include "ec.h"
#include <cstdint>

namespace sc {
using ec::U256;
typedef unsigned __int128 u128;

inline U256 add(U256 a, const U256& b) {
    u128 c = 0;
    for (int i = 0; i < 4; ++i) { c += (u128)a.v[i] + b.v[i]; a.v[i] = (uint64_t)c; c >>= 64; }
    return a;
}
inline U256 sub(U256 a, const U256& b) {
    u128 br = 0;
    for (int i = 0; i < 4; ++i) { u128 d = (u128)a.v[i] - b.v[i] - br; a.v[i] = (uint64_t)d; br = (d >> 64) & 1; }
    return a;
}
inline U256 neg(const U256& a) { U256 z{{0,0,0,0}}; return sub(z, a); }
inline bool is_neg(const U256& a) { return (a.v[3] >> 63) != 0; }
inline bool is_zero(const U256& a) { return (a.v[0] | a.v[1] | a.v[2] | a.v[3]) == 0; }

inline U256 pow2(int e) { U256 r{{0,0,0,0}}; if (e >= 0 && e < 256) r.v[e/64] = 1ull << (e % 64); return r; }

// true iff 0 <= a < 2^bits
inline bool in_range(const U256& a, int bits) {
    if (is_neg(a)) return false;
    for (int b = bits; b < 256; ++b) if ((a.v[b/64] >> (b % 64)) & 1ull) return false;
    return true;
}

template <class RNG>
inline U256 rand_bits(RNG& rng, int bits) {
    U256 r{{0,0,0,0}};
    if (bits <= 0) return r;
    int full = bits / 64, rem = bits % 64;
    for (int i = 0; i < full && i < 4; ++i) r.v[i] = rng();
    if (rem && full < 4) r.v[full] = rng() & ((1ull << rem) - 1);
    return r;
}

} // namespace sc
