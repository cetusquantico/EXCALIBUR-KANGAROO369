// Phase 0 test: verify the clean-room EC math reproduces the known puzzle pubkeys.
// Golden vectors generated independently (see docs). If mul_g(k) compresses to the
// expected pubkey for puzzles 95..135, the whole field+curve stack is correct.
#include "../src/ec.h"
#include <cstdio>
#include <string>
#include <chrono>

struct Vec { int puzzle; const char* priv; const char* pub; };

static const Vec VECS[] = {
    { 95,  "527a792b183c7f64a0e8b1f4",           "02967a5905d6f3b420959a02789f96ab4c3223a2c4d2762f817b7895c5bc88a045" },
    { 100, "af55fc59c335c8ec67ed24826",          "03d2063d40402f030d4cc71331468827aa41a8a09bd6fd801ba77fb64f8e67e617" },
    { 105, "16f14fc2054cd87ee6396b33df3",        "03bcf7ce887ffca5e62c9cabbdb7ffa71dc183c52c04ff4ee5ee82e0c55c39d77b" },
    { 110, "35c0d7234df7deb0f20cf7062444",       "0309976ba5570966bf889196b7fdf5a0f9a1e9ab340556ec29f8bb60599616167d" },
    { 115, "60f4d11574f5deee49961d9609ac6",      "0248d313b0398d4923cdca73b8cfa6532b91b96703902fc8b32fd438a3b7cd7f55" },
    { 120, "b10f22572c497a836ea187f2e1fc23",     "02ceb6cbbcdbdf5ef7150682150f4ce2c6f4807b349827dcdbdd1f2efa885a2630" },
    { 125, "1c533b6bb7f0804e09960225e44877ac",   "0233709eb11e0d4439a729f21c2c443dedb727528229713f0065721ba8fa46f00e" },
    { 130, "33e7665705359f04f28b88cf897c603c9",  "03633cbe3ec02b9401c5effa144c5b4d22f87940259634858fc7e59b1c09937852" },
    { 135, "6d9392a16883f90903d5f78da57af07eb2", "02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16" },
};
// unsolved puzzle 140: public key is exposed, no private key.
static const char* PUB140 = "031f6a332d3c5c4f2de2378c012f429cd109ba07d69690c6c701b6bb87860d6640";

int main() {
    ec::init();
    int pass = 0, fail = 0;

    printf("== Phase 0: EC math validation (mul_g vs known pubkeys) ==\n");
    for (const auto& t : VECS) {
        ec::U256 k = ec::u256_from_hex(t.priv);
        ec::Point P = ec::mul_g(k);
        std::string got = ec::compress(P);
        bool oncurve = ec::is_on_curve(P);
        // also round-trip: decompress the expected pubkey and compare points
        ec::Point D = ec::decompress(t.pub);
        bool rt = !D.inf && ec::fe_eq(D.x, P.x) && ec::fe_eq(D.y, P.y) && ec::is_on_curve(D);
        bool ok = (got == t.pub) && oncurve && rt;
        printf("  puzzle %3d : %s  %s\n", t.puzzle, ok ? "OK " : "FAIL",
               ok ? "" : ("got " + got).c_str());
        ok ? ++pass : ++fail;
    }

    printf("\n== Puzzle 140 (exposed pubkey, unsolved) ==\n");
    ec::Point P140 = ec::decompress(PUB140);
    bool ok140 = !P140.inf && ec::is_on_curve(P140) && (ec::compress(P140) == PUB140);
    printf("  decompress + on-curve + recompress : %s\n", ok140 ? "OK" : "FAIL");
    ok140 ? ++pass : ++fail;

    // micro-benchmark: point multiplications per second (proxy for kangaroo jump rate)
    printf("\n== Benchmark (mul_g throughput) ==\n");
    ec::U256 kb = ec::u256_from_hex("1c533b6bb7f0804e09960225e44877ac");
    const int N = 2000;
    auto t0 = std::chrono::high_resolution_clock::now();
    volatile uint64_t sink = 0;
    for (int i = 0; i < N; ++i) { kb.v[0] += 1; ec::Point P = ec::mul_g(kb); sink ^= P.x.v[0]; }
    auto t1 = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    printf("  %d full scalar mults in %.3f s  -> %.0f mul_g/s (single thread)\n", N, sec, N / sec);
    printf("  (note: a kangaroo 'jump' is ~1 point-add, far cheaper than a full mul_g)\n");

    printf("\n== RESULT: %d passed, %d failed ==\n", pass, fail);
    return fail ? 1 : 0;
}
