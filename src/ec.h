// EXCALIBUR-KANGAROO369 — secp256k1 field & curve math (CPU, clean-room)
// Phase 0: correctness-first. No GPL code; standard formulas only.
#pragma once
#include <cstdint>
#include <string>

namespace ec {

// 256-bit unsigned, little-endian limbs (v[0] = least significant).
struct U256 { uint64_t v[4]; };

// Field element (mod secp256k1 p). Stored reduced in [0, p).
using Fe = U256;

struct Point {
    Fe x, y;
    bool inf;   // point at infinity
};

void init();                                  // sets up constants (call once)

// --- field arithmetic (mod p) ---
Fe   fe_add(const Fe& a, const Fe& b);
Fe   fe_sub(const Fe& a, const Fe& b);
Fe   fe_mul(const Fe& a, const Fe& b);
Fe   fe_sqr(const Fe& a);
Fe   fe_inv(const Fe& a);                     // a^(p-2) mod p (Fermat)
// batch inverse: out[i] = 1/a[i] for all i, using ONE fe_inv + 3(n-1) muls
// (Montgomery's trick). All a[i] must be non-zero.
void fe_batch_inv(const Fe* a, Fe* out, int n);
Fe   fe_neg(const Fe& a);
bool fe_is_zero(const Fe& a);
bool fe_eq(const Fe& a, const Fe& b);

// --- scalars / IO (plain 256-bit, no mod p) ---
U256 u256_from_hex(const std::string& hex);   // big-endian hex, any length <= 64
std::string fe_to_hex(const Fe& a);           // 64-char big-endian

// --- curve ---
Point G();                                    // generator
Point point_add(const Point& a, const Point& b);
Point point_double(const Point& a);
// batched affine add: R[i] = P[i] + Q[i] for all i, sharing ONE batch inversion.
// Requires P[i].x != Q[i].x and no infinities (the kangaroo hot-path case).
void  point_add_batch(const Point* P, const Point* Q, Point* R, int n);
Point mul_g(const U256& k);                   // k * G  (double-and-add, MSB first)
bool  is_on_curve(const Point& p);

// compressed pubkey "02.."/"03.." (66 hex chars)
std::string compress(const Point& p);
// decompress a 33-byte compressed pubkey hex; returns inf=true point on failure
Point decompress(const std::string& hex);

} // namespace ec
