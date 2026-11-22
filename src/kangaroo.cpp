// EXCALIBUR-KANGAROO369 — Pollard's Kangaroo solver.
//   Phase 2    : 2-herd van Oorschot-Wiener kangaroo + distinguished points
//   Phase 3a   : multithread (shared DP table)
//   Phase 3b   : +-P negation-map symmetry + fruitless-cycle escape
//   160-bit    : all distances/exponents tracked as 256-bit (sc::*), so ranges 2..160
//                are correct (time, not integer width, is the only remaining limit).
#include "kangaroo.h"
#include "scalar.h"
#include <vector>
#include <map>
#include <array>
#include <random>
#include <chrono>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>

namespace kang {

using ec::Point; using ec::U256;
using Key = std::array<uint64_t, 4>;
using Val = std::array<uint64_t, 4>;
static inline Key xkey(const Point& p) { return Key{p.x.v[0], p.x.v[1], p.x.v[2], p.x.v[3]}; }
static inline Val vpack(const U256& u) { return Val{u.v[0], u.v[1], u.v[2], u.v[3]}; }
static inline U256 vunpack(const Val& a) { return U256{{a[0], a[1], a[2], a[3]}}; }
static inline Point negp(const Point& p) { return Point{p.x, ec::fe_neg(p.y), p.inf}; }

static uint64_t cap_jumps(int half) { return (half < 55) ? (300ull * (1ull << half)) : ~0ull; }

static U256 rand_even_jump(std::mt19937_64& rng, int half) {
    U256 d = sc::rand_bits(rng, half >= 1 ? half : 1);
    d.v[0] &= ~1ull;
    if (sc::is_zero(d)) d.v[0] = 2;
    return d;
}

// ---------------------------------------------------------------------------
// Plain kangaroo (no symmetry)
// ---------------------------------------------------------------------------
Result solve(const Point& target, int range, uint64_t seed, int threads) {
    Result R;
    if (range < 2 || range > 160) return R;
    if (threads < 1) threads = 1;
    R.threads = threads;

    const int      half = range / 2;
    const int      JMP  = 256, Mloc = std::max(64, 1024 / threads);
    int dp = half - 12; if (dp < 1) dp = 1; if (dp > 24) dp = 24;
    const uint64_t dpmask = (1ull << dp) - 1;
    const uint64_t maxjumps = cap_jumps(half);

    std::vector<U256>  jdist(JMP);
    std::vector<Point> jpt(JMP);
    { std::mt19937_64 rng(seed ^ 0xBADC0FFEE0DDF00DULL);
      for (int i = 0; i < JMP; ++i) { jdist[i] = rand_even_jump(rng, half); jpt[i] = ec::mul_g(jdist[i]); } }

    std::map<Key, std::pair<uint8_t, Val>> table;
    std::mutex mtx; std::atomic<bool> found{false}; std::atomic<uint64_t> gjumps{0}; U256 gkey{{0,0,0,0}};

    auto worker = [&](int tid) {
        std::mt19937_64 rng(seed + (uint64_t)tid * 0x9E3779B97F4A7C15ULL);
        const int T = Mloc / 2, M = 2 * T;
        std::vector<Point> pos(M), Q(M), Rp(M);
        std::vector<U256>  dist(M, U256{{0,0,0,0}}), origin(M);
        std::vector<uint32_t> idx(M);
        for (int i = 0; i < T; ++i) { origin[i] = sc::rand_bits(rng, range); pos[i] = ec::mul_g(origin[i]); }
        for (int j = 0; j < T; ++j) {
            int i = T + j; origin[i] = sc::rand_bits(rng, range);
            pos[i] = sc::is_zero(origin[i]) ? target : ec::point_add(target, ec::mul_g(origin[i]));
        }
        uint64_t local = 0;
        while (!found.load(std::memory_order_relaxed) && gjumps.load(std::memory_order_relaxed) < maxjumps) {
            for (int i = 0; i < M; ++i) { uint32_t id = (uint32_t)(pos[i].x.v[0] % JMP); idx[i] = id; Q[i] = jpt[id]; }
            ec::point_add_batch(pos.data(), Q.data(), Rp.data(), M);
            for (int i = 0; i < M; ++i) { pos[i] = Rp[i]; dist[i] = sc::add(dist[i], jdist[idx[i]]); }
            local += M;
            if ((local & 0xFFFF) == 0) { gjumps.fetch_add(local, std::memory_order_relaxed); local = 0; }
            for (int i = 0; i < M; ++i) {
                if ((pos[i].x.v[0] & dpmask) != 0) continue;
                uint8_t type = (i < T) ? 0 : 1;
                U256 val = sc::add(origin[i], dist[i]);
                std::lock_guard<std::mutex> lk(mtx);
                if (found.load()) break;
                auto it = table.find(xkey(pos[i]));
                if (it == table.end()) { table.emplace(xkey(pos[i]), std::make_pair(type, vpack(val))); continue; }
                if (it->second.first == type) continue;
                U256 vT = (type == 0) ? val : vunpack(it->second.second);
                U256 vW = (type == 1) ? val : vunpack(it->second.second);
                U256 k = sc::sub(vT, vW);
                if (!sc::in_range(k, range)) continue;
                Point chk = ec::mul_g(k);
                if (ec::fe_eq(chk.x, target.x) && ec::fe_eq(chk.y, target.y)) { gkey = k; found.store(true); }
            }
        }
        gjumps.fetch_add(local, std::memory_order_relaxed);
    };

    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker, t);
    for (auto& th : pool) th.join();
    auto t1 = std::chrono::high_resolution_clock::now();
    R.seconds = std::chrono::duration<double>(t1 - t0).count();
    R.found = found.load(); R.key = gkey; R.jumps = gjumps.load(); R.dp_count = table.size();
    R.K = (double)R.jumps / std::pow(2.0, range / 2.0);
    return R;
}

// ---------------------------------------------------------------------------
// Kangaroo WITH +-P symmetry (negation map) + fruitless-cycle escape
// ---------------------------------------------------------------------------
Result solve_sym(const Point& target, int range, uint64_t seed, int threads) {
    Result R;
    if (range < 2 || range > 160) return R;
    if (threads < 1) threads = 1;
    R.threads = threads;

    const int      half = range / 2;
    const int      JMP  = 256, Mloc = std::max(64, 1024 / threads);
    int dp = half - 12; if (dp < 1) dp = 1; if (dp > 24) dp = 24;
    const uint64_t dpmask = (1ull << dp) - 1;
    const uint64_t maxjumps = cap_jumps(half);
    const U256     N2 = sc::pow2(range - 1);
    const Point    target_c = ec::point_add(target, negp(ec::mul_g(N2)));   // center to [-N2,N2)

    std::vector<U256>  jdist(JMP);
    std::vector<Point> jpt(JMP), njpt(JMP);
    { std::mt19937_64 rng(seed ^ 0xBADC0FFEE0DDF00DULL);
      for (int i = 0; i < JMP; ++i) { jdist[i] = rand_even_jump(rng, half); jpt[i] = ec::mul_g(jdist[i]); njpt[i] = negp(jpt[i]); } }

    std::map<Key, std::pair<uint8_t, Val>> table;
    std::mutex mtx; std::atomic<bool> found{false}; std::atomic<uint64_t> gjumps{0}; U256 gkey{{0,0,0,0}};

    auto worker = [&](int tid) {
        std::mt19937_64 rng(seed + (uint64_t)tid * 0x9E3779B97F4A7C15ULL);
        const int T = Mloc / 2, M = 2 * T;
        std::vector<Point> pos(M), prev(M), prev2(M), Q(M), Rp(M);
        std::vector<U256>  dist(M, U256{{0,0,0,0}}), origin(M);
        std::vector<uint32_t> idx(M);
        std::vector<int> sgn(M);
        for (int i = 0; i < T; ++i) { origin[i] = sc::rand_bits(rng, range - 1); pos[i] = ec::mul_g(origin[i]); }
        for (int j = 0; j < T; ++j) {
            int i = T + j; origin[i] = sc::rand_bits(rng, range - 1);
            pos[i] = sc::is_zero(origin[i]) ? target_c : ec::point_add(target_c, ec::mul_g(origin[i]));
        }
        for (int i = 0; i < M; ++i) { prev[i] = pos[i]; prev2[i] = pos[i]; }
        uint64_t local = 0;
        while (!found.load(std::memory_order_relaxed) && gjumps.load(std::memory_order_relaxed) < maxjumps) {
            for (int i = 0; i < M; ++i) {
                uint32_t base = (uint32_t)(pos[i].x.v[0] % JMP);
                if (ec::fe_eq(pos[i].x, prev2[i].x)) base = (base + 1) % JMP;   // escape 2-cycle
                bool even = ((pos[i].y.v[0] & 1ull) == 0);
                idx[i] = base; sgn[i] = even ? 1 : -1;
                Q[i] = even ? jpt[base] : njpt[base];
            }
            ec::point_add_batch(pos.data(), Q.data(), Rp.data(), M);
            for (int i = 0; i < M; ++i) {
                prev2[i] = prev[i]; prev[i] = pos[i]; pos[i] = Rp[i];
                dist[i] = (sgn[i] > 0) ? sc::add(dist[i], jdist[idx[i]]) : sc::sub(dist[i], jdist[idx[i]]);
            }
            local += M;
            if ((local & 0xFFFF) == 0) { gjumps.fetch_add(local, std::memory_order_relaxed); local = 0; }
            for (int i = 0; i < M; ++i) {
                if ((pos[i].x.v[0] & dpmask) != 0) continue;
                uint8_t type = (i < T) ? 0 : 1;
                U256 val = sc::add(origin[i], dist[i]);
                std::lock_guard<std::mutex> lk(mtx);
                if (found.load()) break;
                auto it = table.find(xkey(pos[i]));
                if (it == table.end()) { table.emplace(xkey(pos[i]), std::make_pair(type, vpack(val))); continue; }
                if (it->second.first == type) continue;
                U256 vT = (type == 0) ? val : vunpack(it->second.second);
                U256 vW = (type == 1) ? val : vunpack(it->second.second);
                U256 cand[2] = { sc::sub(vT, vW), sc::neg(sc::add(vT, vW)) };
                for (int c = 0; c < 2 && !found.load(); ++c) {
                    U256 k = sc::add(cand[c], N2);
                    if (!sc::in_range(k, range)) continue;
                    Point chk = ec::mul_g(k);
                    if (ec::fe_eq(chk.x, target.x) && ec::fe_eq(chk.y, target.y)) { gkey = k; found.store(true); }
                }
            }
        }
        gjumps.fetch_add(local, std::memory_order_relaxed);
    };

    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker, t);
    for (auto& th : pool) th.join();
    auto t1 = std::chrono::high_resolution_clock::now();
    R.seconds = std::chrono::duration<double>(t1 - t0).count();
    R.found = found.load(); R.key = gkey; R.jumps = gjumps.load(); R.dp_count = table.size();
    R.K = (double)R.jumps / std::pow(2.0, range / 2.0);
    return R;
}

} // namespace kang
