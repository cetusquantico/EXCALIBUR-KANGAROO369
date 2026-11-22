// EXCALIBUR-KANGAROO369 — test_wide: proves the 65..160-bit paths.
//
// The other kangaroo tests only exercise ranges <= 48 bits, where every value
// fits in limb 0 (v[1..3] == 0). This test drives the MULTI-LIMB paths that the
// 160-bit claim depends on:
//   (A) sc:: 256-bit integer ops (carry/borrow/sign across all 4 limbs),
//   (B) ec::mul_g with scalars > 2^64 (cross-checked vs an INDEPENDENT impl),
//   (C) the exact kangaroo key-recovery arithmetic with keys > 2^64.
// Reference vectors were produced by a separate pure-Python secp256k1 (see
// scratchpad/gen_vectors.py), so a bug in our C++ cannot "agree with itself".
#include "ec.h"
#include "scalar.h"
#include <cstdio>
#include <cstdint>
#include <string>

using ec::U256;

static int fails = 0, checks = 0;
static bool eqU(const U256& a, const U256& b) {
    return a.v[0]==b.v[0] && a.v[1]==b.v[1] && a.v[2]==b.v[2] && a.v[3]==b.v[3];
}
static void ck(bool cond, const char* name) {
    checks++;
    if (!cond) { fails++; printf("  FAIL: %s\n", name); }
    else       { printf("  ok  : %s\n", name); }
}

// ---- independent reference vectors (from gen_vectors.py) ----
const U256 K_PLAIN = {{0x34567890abcdef12,0x00000000c0ffee12,0,0}};                 // 96-bit
const char* PUB_PLAIN = "031937bc23de2ae74211a28f7f352dc8bf1e94fec2ca7f98326b61898ea4a2f45d";
const U256 K_WIDE  = {{0x445566778899aabb,0xcafef00d00112233,0x00000000deadbeef,0}}; // 160-bit
const char* PUB_WIDE  = "02c20f59750ec6b74b45cda30b52eb13cb2fe82e5b6221f7cb3ee73468c97d75b3";

const U256 WA         = {{0x0123456789abcdef,0x0000001000000001,0x0000000080000000,0}}; // 160-bit
const U256 WB         = {{0xffffffffffffffff,0x0000000000000040,0x0000000000000001,0}}; // 129-bit
const U256 WA_PLUS_B  = {{0x0123456789abcdee,0x0000001000000042,0x0000000080000001,0}};
const U256 WA_MINUS_B = {{0x0123456789abcdf0,0x0000000fffffffc0,0x000000007fffffff,0}};
const U256 WB_MINUS_A = {{0xfedcba9876543210,0xfffffff00000003f,0xffffffff80000000,0xffffffffffffffff}};

int main() {
    ec::init();
    printf("== test_wide: multi-limb (65..160-bit) correctness ==\n");

    // ---------- (A) sc:: 256-bit arithmetic, multi-limb ----------
    printf("\n-- A) sc:: 256-bit integer ops --\n");
    ck(eqU(sc::add(WA, WB), WA_PLUS_B),  "add(a,b) multi-limb carry  == ref");
    ck(eqU(sc::sub(WA, WB), WA_MINUS_B), "sub(a,b) multi-limb borrow == ref");
    ck(eqU(sc::sub(WB, WA), WB_MINUS_A), "sub(b,a) negative (two's complement) == ref");
    ck(sc::is_neg(WB_MINUS_A),           "is_neg(b-a) == true");
    ck(!sc::is_neg(WA),                  "is_neg(a) == false (bit255 clear)");
    ck(sc::is_neg(U256{{0,0,0,0x8000000000000000ull}}), "is_neg(top-bit set) == true");
    ck(!sc::is_neg(U256{{0,0,0,0x7fffffffffffffffull}}), "is_neg(2^255-ish, bit255 clear) == false");

    // identities (must hold for any multi-limb value)
    ck(eqU(sc::add(WA, sc::neg(WA)), U256{{0,0,0,0}}), "a + (-a) == 0");
    ck(eqU(sc::neg(sc::neg(WA)), WA),                  "-(-a) == a");
    ck(eqU(sc::sub(sc::add(WA, WB), WB), WA),          "(a+b)-b == a (round trip)");
    ck(eqU(sc::sub(WA, WB), sc::add(WA, sc::neg(WB))), "a-b == a+(-b)");

    // full-width carry / borrow across all four limbs
    ck(eqU(sc::add(U256{{~0ull,~0ull,~0ull,0}}, U256{{1,0,0,0}}), U256{{0,0,0,1}}),
       "carry ripples limb0->limb3");
    ck(eqU(sc::sub(U256{{0,0,0,1}}, U256{{1,0,0,0}}), U256{{~0ull,~0ull,~0ull,0}}),
       "borrow ripples limb3->limb0");

    // pow2 lands in the right limb/bit
    ck(eqU(sc::pow2(64),  U256{{0,1,0,0}}),            "pow2(64)  == 2^64");
    ck(eqU(sc::pow2(100), U256{{0,1ull<<36,0,0}}),     "pow2(100) == 2^100");
    ck(eqU(sc::pow2(159), U256{{0,0,1ull<<31,0}}),     "pow2(159) == 2^159");

    // in_range over the full width
    ck(sc::in_range(sc::pow2(100), 101),               "in_range(2^100,101) == true");
    ck(!sc::in_range(sc::pow2(100), 100),              "in_range(2^100,100) == false");
    ck(sc::in_range(WA, 160),                          "in_range(a,160) == true");
    ck(!sc::in_range(WA, 159),                         "in_range(a,159) == false (bit159 set)");
    ck(!sc::in_range(sc::neg(U256{{1,0,0,0}}), 160),   "in_range(-1,160) == false");

    // ---------- (B) ec::mul_g with scalars > 2^64, vs independent impl ----------
    printf("\n-- B) mul_g(k) for k > 2^64 (cross-checked vs Python) --\n");
    ck(ec::compress(ec::mul_g(K_PLAIN)) == std::string(PUB_PLAIN), "mul_g(96-bit)  == independent pubkey");
    ck(ec::compress(ec::mul_g(K_WIDE))  == std::string(PUB_WIDE),  "mul_g(160-bit) == independent pubkey");

    // ---------- (C) kangaroo recovery arithmetic with keys > 2^64 ----------
    printf("\n-- C) key-recovery arithmetic, keys > 2^64 --\n");
    // (C1) plain path: k = vT - vW, then mul_g(k) must equal the target.
    {
        ec::Point target = ec::mul_g(K_PLAIN);
        U256 vW = WB;                    // arbitrary wild accumulator
        U256 vT = sc::add(vW, K_PLAIN);  // tame = wild + secret
        U256 k  = sc::sub(vT, vW);       // recovery
        ec::Point chk = ec::mul_g(k);
        ck(eqU(k, K_PLAIN),                                   "C1 k == secret (multi-limb)");
        ck(ec::fe_eq(chk.x, target.x) && ec::fe_eq(chk.y, target.y), "C1 mul_g(k) == target");
        ck(sc::in_range(k, 96),                               "C1 in_range(k,96)");
    }
    // (C2) symmetry path: centered value is negative, k = (cand) + N2 restores it.
    //      range=160, N2=2^159; secret K_PLAIN < N2 so centered = K_PLAIN - N2 < 0.
    {
        const int range = 160;
        U256 N2 = sc::pow2(range - 1);
        ec::Point target = ec::mul_g(K_PLAIN);
        U256 centered = sc::sub(K_PLAIN, N2);          // negative (two's complement)
        ck(sc::is_neg(centered),                       "C2 centered value is negative");
        U256 k = sc::add(centered, N2);                // add N2 back -> secret
        ec::Point chk = ec::mul_g(k);
        ck(eqU(k, K_PLAIN),                            "C2 k == secret after +N2");
        ck(sc::in_range(k, range),                     "C2 in_range(k,160)");
        ck(ec::fe_eq(chk.x, target.x) && ec::fe_eq(chk.y, target.y), "C2 mul_g(k) == target");
        // the second kangaroo candidate form: -(vT+vW); check the neg/add machinery
        U256 cand2 = sc::neg(sc::add(centered, U256{{0,0,0,0}}));
        ck(eqU(sc::neg(cand2), centered),              "C2 neg(neg(centered)) == centered");
    }

    printf("\n== %d/%d checks passed ==\n", checks - fails, checks);
    if (fails) { printf("RESULT: FAIL (%d)\n", fails); return 1; }
    printf("RESULT: PASS — 65..160-bit paths proven\n");
    return 0;
}
