// EXCALIBUR-KANGAROO369 — secp256k1 field & curve math (CPU, clean-room)
// Phase 0: correctness-first implementation. Standard formulas only (no GPL).
//
// Field: p = 2^256 - 0x1000003D1  (secp256k1). 2^256 ≡ 0x1000003D1 (mod p).
// Representation: U256 = uint64_t[4], little-endian.
#include "ec.h"
#include <cstring>
#include <stdexcept>
#include <vector>

namespace ec {

typedef unsigned __int128 u128;

// p and generator, filled in init()
static uint64_t P[4];
static uint64_t EXP_SQRT[4];   // (p+1)/4, for modular sqrt
static Point    G_POINT;
static const u128 C = 0x1000003D1ULL;   // 2^256 mod p

// ---------- low-level helpers ----------
static bool ge_p(const uint64_t r[4]) {          // r >= p ?
    for (int i = 3; i >= 0; --i) {
        if (r[i] != P[i]) return r[i] > P[i];
    }
    return true; // equal
}
static void sub_p(uint64_t r[4]) {               // r -= p (assumes r >= p)
    u128 borrow = 0;
    for (int i = 0; i < 4; ++i) {
        u128 cur = (u128)r[i] - P[i] - borrow;
        r[i] = (uint64_t)cur;
        borrow = (cur >> 64) & 1;
    }
}
static void cond_sub_p(uint64_t r[4]) { if (ge_p(r)) sub_p(r); }

// reduce a value held in 5 limbs (v[4] is the small overflow) mod p, into r[4]
static void fold5(uint64_t v[5], uint64_t r[4]) {
    while (v[4]) {
        u128 acc = (u128)v[4] * C;
        v[4] = 0;
        u128 c = (u128)v[0] + (uint64_t)acc; v[0] = (uint64_t)c; c >>= 64; acc >>= 64;
        c += (u128)v[1] + (uint64_t)acc;     v[1] = (uint64_t)c; c >>= 64;
        c += v[2];                           v[2] = (uint64_t)c; c >>= 64;
        c += v[3];                           v[3] = (uint64_t)c; c >>= 64;
        v[4] = (uint64_t)c;
    }
    r[0] = v[0]; r[1] = v[1]; r[2] = v[2]; r[3] = v[3];
    cond_sub_p(r);
    cond_sub_p(r);
}

// reduce a 512-bit product t[0..7] mod p
static void reduce512(const uint64_t t[8], uint64_t r[4]) {
    uint64_t m[5]; u128 acc = 0;
    for (int i = 0; i < 4; ++i) { acc += (u128)t[4 + i] * C; m[i] = (uint64_t)acc; acc >>= 64; }
    m[4] = (uint64_t)acc;
    uint64_t s[5]; acc = 0;
    for (int i = 0; i < 4; ++i) { acc += (u128)t[i] + m[i]; s[i] = (uint64_t)acc; acc >>= 64; }
    acc += m[4]; s[4] = (uint64_t)acc;
    fold5(s, r);
}

// ---------- field arithmetic ----------
Fe fe_add(const Fe& a, const Fe& b) {
    Fe r; u128 c = 0;
    for (int i = 0; i < 4; ++i) { c += (u128)a.v[i] + b.v[i]; r.v[i] = (uint64_t)c; c >>= 64; }
    if (c || ge_p(r.v)) sub_p(r.v);
    return r;
}
Fe fe_sub(const Fe& a, const Fe& b) {
    Fe r; u128 borrow = 0;
    for (int i = 0; i < 4; ++i) { u128 cur = (u128)a.v[i] - b.v[i] - borrow; r.v[i] = (uint64_t)cur; borrow = (cur >> 64) & 1; }
    if (borrow) { // add p back
        u128 c = 0;
        for (int i = 0; i < 4; ++i) { c += (u128)r.v[i] + P[i]; r.v[i] = (uint64_t)c; c >>= 64; }
    }
    return r;
}
Fe fe_neg(const Fe& a) { Fe z{{0,0,0,0}}; return fe_is_zero(a) ? z : fe_sub(z, a); }

Fe fe_mul(const Fe& a, const Fe& b) {
    uint64_t t[8]; memset(t, 0, sizeof(t));
    for (int i = 0; i < 4; ++i) {
        u128 carry = 0;
        for (int j = 0; j < 4; ++j) {
            u128 cur = (u128)a.v[i] * b.v[j] + t[i + j] + carry;
            t[i + j] = (uint64_t)cur; carry = cur >> 64;
        }
        // propagate remaining carry
        int k = i + 4; uint64_t cc = (uint64_t)carry;
        while (cc && k < 8) { u128 s = (u128)t[k] + cc; t[k] = (uint64_t)s; cc = (uint64_t)(s >> 64); ++k; }
    }
    Fe r; reduce512(t, r.v); return r;
}
Fe fe_sqr(const Fe& a) { return fe_mul(a, a); }

bool fe_is_zero(const Fe& a) { return (a.v[0] | a.v[1] | a.v[2] | a.v[3]) == 0; }
bool fe_eq(const Fe& a, const Fe& b) { return !memcmp(a.v, b.v, 32); }

// generic exponentiation a^e mod p (e as 4-limb)
static Fe fe_pow(const Fe& a, const uint64_t e[4]) {
    Fe r{{1,0,0,0}}, base = a;
    for (int limb = 0; limb < 4; ++limb) {
        uint64_t w = e[limb];
        for (int b = 0; b < 64; ++b) {
            if (w & 1ULL) r = fe_mul(r, base);
            base = fe_sqr(base);
            w >>= 1;
        }
    }
    return r;
}
Fe fe_inv(const Fe& a) {
    uint64_t e[4]; memcpy(e, P, sizeof(e)); // p-2
    u128 borrow = 2;
    for (int i = 0; i < 4 && borrow; ++i) { u128 cur = (u128)e[i] - borrow; e[i] = (uint64_t)cur; borrow = (cur >> 64) & 1; }
    return fe_pow(a, e);
}

// batch inverse (Montgomery's trick): 1 inversion + 3(n-1) muls
void fe_batch_inv(const Fe* a, Fe* out, int n) {
    if (n <= 0) return;
    std::vector<Fe> pref(n);
    pref[0] = a[0];
    for (int i = 1; i < n; ++i) pref[i] = fe_mul(pref[i - 1], a[i]);
    Fe inv = fe_inv(pref[n - 1]);              // the single expensive inversion
    for (int i = n - 1; i > 0; --i) {
        out[i] = fe_mul(inv, pref[i - 1]);
        inv = fe_mul(inv, a[i]);
    }
    out[0] = inv;
}

// ---------- IO ----------
U256 u256_from_hex(const std::string& hex0) {
    std::string hex = hex0;
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);
    if (hex.size() > 64) throw std::runtime_error("hex too long");
    std::string h(64 - hex.size(), '0'); h += hex;
    U256 r{{0,0,0,0}};
    auto nib = [](char c)->int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("bad hex");
    };
    // h is big-endian: h[0..1] = most significant byte
    for (int i = 0; i < 32; ++i) {
        int hi = nib(h[2 * i]);
        int lo = nib(h[2 * i + 1]);
        uint8_t byte = (uint8_t)((hi << 4) | lo);   // h[0..1] = most significant byte
        int bytepos = 31 - i;                       // little-endian byte index
        r.v[bytepos / 8] |= (uint64_t)byte << (8 * (bytepos % 8));
    }
    return r;
}
std::string fe_to_hex(const Fe& a) {
    static const char* d = "0123456789abcdef";
    std::string s(64, '0');
    for (int bytepos = 0; bytepos < 32; ++bytepos) {
        uint8_t byte = (uint8_t)(a.v[bytepos / 8] >> (8 * (bytepos % 8)));
        int outpos = (31 - bytepos) * 2;            // big-endian output
        s[outpos] = d[byte >> 4];
        s[outpos + 1] = d[byte & 0xf];
    }
    return s;
}

// ---------- curve ----------
Point G() { return G_POINT; }

Point point_double(const Point& a) {
    if (a.inf || fe_is_zero(a.y)) return Point{{},{},true};
    // lambda = 3x^2 / (2y)
    Fe x2 = fe_sqr(a.x);
    Fe three_x2 = fe_add(fe_add(x2, x2), x2);
    Fe two_y = fe_add(a.y, a.y);
    Fe lam = fe_mul(three_x2, fe_inv(two_y));
    Fe x3 = fe_sub(fe_sqr(lam), fe_add(a.x, a.x));
    Fe y3 = fe_sub(fe_mul(lam, fe_sub(a.x, x3)), a.y);
    return Point{x3, y3, false};
}
Point point_add(const Point& a, const Point& b) {
    if (a.inf) return b;
    if (b.inf) return a;
    if (fe_eq(a.x, b.x)) {
        if (fe_eq(a.y, b.y)) return point_double(a);
        return Point{{},{},true}; // a == -b
    }
    Fe lam = fe_mul(fe_sub(b.y, a.y), fe_inv(fe_sub(b.x, a.x)));
    Fe x3 = fe_sub(fe_sub(fe_sqr(lam), a.x), b.x);
    Fe y3 = fe_sub(fe_mul(lam, fe_sub(a.x, x3)), a.y);
    return Point{x3, y3, false};
}
void point_add_batch(const Point* P, const Point* Q, Point* R, int n) {
    if (n <= 0) return;
    std::vector<Fe> den(n), dinv(n);
    for (int i = 0; i < n; ++i) den[i] = fe_sub(Q[i].x, P[i].x);   // x2 - x1 (!= 0)
    fe_batch_inv(den.data(), dinv.data(), n);                      // one inversion total
    for (int i = 0; i < n; ++i) {
        Fe lam = fe_mul(fe_sub(Q[i].y, P[i].y), dinv[i]);
        Fe x3  = fe_sub(fe_sub(fe_sqr(lam), P[i].x), Q[i].x);
        Fe y3  = fe_sub(fe_mul(lam, fe_sub(P[i].x, x3)), P[i].y);
        R[i] = Point{x3, y3, false};
    }
}

Point mul_g(const U256& k) {
    Point r{{},{},true};
    // MSB-first double-and-add
    for (int limb = 3; limb >= 0; --limb) {
        for (int b = 63; b >= 0; --b) {
            r = point_double(r);
            if ((k.v[limb] >> b) & 1ULL) r = point_add(r, G_POINT);
        }
    }
    return r;
}
bool is_on_curve(const Point& p) {
    if (p.inf) return true;
    Fe y2 = fe_sqr(p.y);
    Fe x3 = fe_mul(fe_sqr(p.x), p.x);
    Fe seven{{7,0,0,0}};
    return fe_eq(y2, fe_add(x3, seven));
}

std::string compress(const Point& p) {
    std::string s = (p.y.v[0] & 1) ? "03" : "02";
    return s + fe_to_hex(p.x);
}
Point decompress(const std::string& hex) {
    if (hex.size() != 66) return Point{{},{},true};
    int prefix = (hex[1] == '3') ? 3 : 2;
    Fe x = u256_from_hex(hex.substr(2));
    Fe seven{{7,0,0,0}};
    Fe rhs = fe_add(fe_mul(fe_sqr(x), x), seven);   // x^3 + 7
    Fe y = fe_pow(rhs, EXP_SQRT);                   // sqrt = rhs^((p+1)/4)
    // verify it is a real sqrt
    if (!fe_eq(fe_sqr(y), rhs)) return Point{{},{},true};
    if ((int)(y.v[0] & 1) != (prefix & 1)) y = fe_neg(y);
    Point p{x, y, false};
    return p;
}

// ---------- init ----------
void init() {
    P[0] = 0xFFFFFFFEFFFFFC2FULL; P[1] = 0xFFFFFFFFFFFFFFFFULL;
    P[2] = 0xFFFFFFFFFFFFFFFFULL; P[3] = 0xFFFFFFFFFFFFFFFFULL;
    // EXP_SQRT = (p+1)/4 ; compute (p+1) then >> 2
    uint64_t pp1[4]; memcpy(pp1, P, sizeof(pp1));
    u128 c = 1;
    for (int i = 0; i < 4 && c; ++i) { u128 s = (u128)pp1[i] + c; pp1[i] = (uint64_t)s; c = s >> 64; }
    for (int i = 0; i < 4; ++i) {
        uint64_t hi = (i < 3) ? pp1[i + 1] : 0;
        EXP_SQRT[i] = (pp1[i] >> 2) | (hi << 62);
    }
    G_POINT.inf = false;
    G_POINT.x.v[0] = 0x59F2815B16F81798ULL; G_POINT.x.v[1] = 0x029BFCDB2DCE28D9ULL;
    G_POINT.x.v[2] = 0x55A06295CE870B07ULL; G_POINT.x.v[3] = 0x79BE667EF9DCBBACULL;
    G_POINT.y.v[0] = 0x9C47D08FFB10D4B8ULL; G_POINT.y.v[1] = 0xFD17B448A6855419ULL;
    G_POINT.y.v[2] = 0x5DA4FBFC0E1108A8ULL; G_POINT.y.v[3] = 0x483ADA7726A3C465ULL;
}

} // namespace ec
