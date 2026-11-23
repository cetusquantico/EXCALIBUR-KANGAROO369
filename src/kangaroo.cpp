// EXCALIBUR-KANGAROO369 — Pollard's Kangaroo solver.
//   Phase 2    : 2-herd van Oorschot-Wiener kangaroo + distinguished points
//   Phase 3a   : multithread (shared DP table)
//   Phase 3b   : +-P negation-map symmetry
//   Phase 6    : symmetry rebuilt as Gaudry–Schost walks (restart after every DP,
//                2/4-cycle escape, in-batch teleport restarts) — the 3b design stalled
//                above ~52 bits (walks trapped in long self-intersection cycles)
//   160-bit    : all distances/exponents tracked as 256-bit (sc::*), so ranges 2..160
//                are correct (time, not integer width, is the only remaining limit).
#include "kangaroo.h"
#include "scalar.h"
#include "checkpoint.h"
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
static inline Point mulg_signed(const U256& k) {          // k*G for a two's-complement signed k
    return sc::is_neg(k) ? negp(ec::mul_g(sc::neg(k))) : ec::mul_g(k);
}

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
// Kangaroo WITH +-P symmetry (negation map) — Phase 6: Gaudry–Schost walks.
//
// The negation-map step (jump sign chosen by y-parity) is a function of the class
// {P,-P}, but it turns the kangaroo into a *symmetric 1-D random walk* in |dlog|.
// Such a walk over a finite jump set is recurrent: it returns to exact points after
// ~sigma^(2/3) steps (sigma ~ jump size), closing long cycles that re-hit known DPs
// forever, while short fruitless 2-/4-cycles appear at rates ~1/(2r) and ~1/(4r^2).
// The Phase 3b design let walks run on indefinitely, so above ~52 bits they spent
// their time trapped (measured at 54 bits: more same-herd re-hits than new DPs and
// zero tame/wild matches).
//
// Phase 6 uses the negation map the standard way for an interval (Gaudry–Schost):
//   1. after EVERY distinguished point the walk restarts at a fresh, uniformly random
//      spot of its herd's region — a precomputed random "teleport" t*G folded into the
//      next batch add, so a restart costs one batched addition (no scalar multiply);
//   2. the trail length 2^dp is kept well below the self-intersection time;
//   3. 2- and 4-cycles are escaped with a deterministic, class-invariant jump drawn
//      from a separate table of larger distances (so the escape can't re-enter it);
//   4. backstop: a walk that goes 8*2^dp steps without a DP is teleported as well.
// Regions (centred coordinates, class space |.|): tame starts uniform in [0, N2),
// wild starts uniform in k_c + [-N2/2, N2/2).
// ---------------------------------------------------------------------------
Result solve_sym(const Point& target, int range, uint64_t seed, int threads, CkptOpts* ck) {
    Result R;
    if (range < 2 || range > 160) return R;
    if (threads < 1) threads = 1;
    R.threads = threads;

    const int      half = range / 2;
    const int      JMP = 1024, ESC = 64, TEL = 1024;
    const int      Mloc = std::max(64, 1024 / threads);
    // Trail length 2^dp well below the self-intersection time sigma^(2/3), sigma ~ 2^half
    // (keeps the chance that a trail closes a long loop around 1%).
    int dp = (2 * (half - 6)) / 3 - 1; if (dp < 4) dp = 4; if (dp > 24) dp = 24;
    const uint64_t dpmask      = (1ull << dp) - 1;
    const uint64_t maxjumps    = (ck && ck->max_jumps) ? ck->max_jumps : cap_jumps(half);
    const uint64_t salt        = ck ? ck->session_salt : 0;
    const uint64_t stall_limit = (uint64_t)8 << dp;
    const U256     N2 = sc::pow2(range - 1);                      // centring offset / tame region
    const U256     W2 = sc::pow2(range - 2);                      // wild half-width (= N2/2)
    const Point    negN2G   = negp(ec::mul_g(N2));
    const Point    target_c = ec::point_add(target, negN2G);      // centre to [-N2, N2)
    if (target_c.inf) { R.found = true; R.key = N2; return R; }   // k == N2 exactly

    // Phase 5: resume loads the DP table; the walk tables must match the ones that built
    // those DPs, so on a valid resume we reuse the stored seed.
    Snapshot snap; bool resumed = false; uint64_t jseed = seed;
    if (ck && ck->resume && !ck->path.empty()) {
        if (ckpt_load(ck->path, snap) == 0 && snap.range == range && snap.sym == 1 &&
            ec::fe_eq(snap.target.x, target.x) && ec::fe_eq(snap.target.y, target.y)) {
            jseed = snap.seed; resumed = true;
        }
    }

    // Walk tables, all derived from jseed (a resume rebuilds the identical walk):
    //   normal   +-jdist[j]*G, j = x.v[1] % JMP,         jdist even, < 2^half
    //   escape   +-edist[e]*G, e = (x.v[1] >> 20) % ESC,  edist even, in [2^half, 2^(half+1))
    //   teleport tdist[r]*G, or (tdist[r]-N2)*G to wrap,  tdist uniform in [1, N2)
    std::vector<U256>  jdist(JMP), njdist(JMP), edist(ESC), nedist(ESC), tdist(TEL), twdist(TEL);
    std::vector<Point> jpt(JMP), njpt(JMP), ept(ESC), nept(ESC), tpt(TEL), tpw(TEL);
    {
        std::mt19937_64 rng(jseed ^ 0xBADC0FFEE0DDF00DULL);
        for (int i = 0; i < JMP; ++i) {
            jdist[i] = rand_even_jump(rng, half); njdist[i] = sc::neg(jdist[i]);
            jpt[i] = ec::mul_g(jdist[i]); njpt[i] = negp(jpt[i]);
        }
        const U256 eb = sc::pow2(half >= 1 ? half : 1);
        for (int i = 0; i < ESC; ++i) {
            edist[i] = sc::add(eb, rand_even_jump(rng, half)); nedist[i] = sc::neg(edist[i]);
            ept[i] = ec::mul_g(edist[i]); nept[i] = negp(ept[i]);
        }
        for (int i = 0; i < TEL; ++i) {
            tdist[i] = sc::rand_bits(rng, range - 1); if (sc::is_zero(tdist[i])) tdist[i].v[0] = 1;
            twdist[i] = sc::sub(tdist[i], N2);
            tpt[i] = ec::mul_g(tdist[i]);
            tpw[i] = ec::point_add(tpt[i], negN2G);
        }
    }

    std::map<Key, std::pair<uint8_t, Val>> table;
    std::mutex mtx; std::atomic<bool> found{false}; std::atomic<uint64_t> gjumps{0}; U256 gkey{{0,0,0,0}};
    std::atomic<uint64_t> a_esc{0}, a_rst{0}, a_col{0}, a_reh{0};   // diagnostics

    if (resumed) {
        for (const auto& d : snap.dps)
            table.emplace(Key{d.x.v[0], d.x.v[1], d.x.v[2], d.x.v[3]},
                          std::make_pair(d.type, Val{d.val.v[0], d.val.v[1], d.val.v[2], d.val.v[3]}));
        gjumps.store(snap.jumps, std::memory_order_relaxed);
        if (ck) ck->loaded_dps = table.size();
    }

    // Periodic snapshot writer: copy the table under lock, then write atomically off-lock.
    auto snapshot_now = [&]() {
        Snapshot s; s.range = range; s.sym = 1; s.seed = jseed; s.target = target;
        {
            std::lock_guard<std::mutex> lk(mtx);
            s.jumps = gjumps.load(std::memory_order_relaxed);
            s.dps.reserve(table.size());
            for (const auto& kv : table) {
                DP d; d.x = U256{{kv.first[0], kv.first[1], kv.first[2], kv.first[3]}};
                d.type = kv.second.first;
                d.val  = U256{{kv.second.second[0], kv.second.second[1], kv.second.second[2], kv.second.second[3]}};
                s.dps.push_back(d);
            }
        }
        ckpt_save(ck->path, s);
    };
    std::atomic<bool> writer_stop{false};
    std::thread writer;
    if (ck && !ck->path.empty() && ck->interval_sec > 0) {
        writer = std::thread([&]() {
            while (!writer_stop.load(std::memory_order_relaxed)) {
                for (int i = 0; i < ck->interval_sec * 10 && !writer_stop.load(std::memory_order_relaxed); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (writer_stop.load(std::memory_order_relaxed)) break;
                snapshot_now();
            }
        });
    }

    auto worker = [&](int tid) {
        std::mt19937_64 rng(seed + (uint64_t)tid * 0x9E3779B97F4A7C15ULL + salt);
        const int T = Mloc / 2, M = 2 * T;          // walks [0,T) tame, [T,M) wild
        const uint64_t EMPTY = ~0ull;               // empty history slot
        std::vector<Point> pos(M), Q(M), Rp(M);
        std::vector<U256>  dist(M, U256{{0,0,0,0}}), origin(M), stepd(M);
        std::vector<uint64_t> hist(4 * (size_t)M, EMPTY);   // ring: x.v[0] of the last 4 points
        std::vector<uint64_t> stall(M, 0);
        std::vector<uint8_t>  restart(M, 0);
        uint64_t l_esc = 0, l_rst = 0, l_col = 0, l_reh = 0;

        // Initial placement (one scalar multiply per walk; later restarts are teleports).
        for (int i = 0; i < M; ++i) {
            for (;;) {
                if (i < T) {
                    origin[i] = sc::rand_bits(rng, range - 1);
                    if (sc::is_zero(origin[i])) continue;
                    pos[i] = ec::mul_g(origin[i]);
                } else {
                    origin[i] = sc::sub(sc::rand_bits(rng, range - 1), W2);   // [-W2, W2)
                    pos[i] = sc::is_zero(origin[i]) ? target_c
                                                    : ec::point_add(target_c, mulg_signed(origin[i]));
                }
                if (!pos[i].inf) break;
            }
        }

        uint64_t local = 0, t = 0;
        while (!found.load(std::memory_order_relaxed) && gjumps.load(std::memory_order_relaxed) < maxjumps
               && (!ck || !ck->stop || !ck->stop->load(std::memory_order_relaxed))) {
            const size_t s4 = (size_t)(t & 3) * M, s2 = (size_t)((t + 2) & 3) * M;   // slots of t-4, t-2
            for (int i = 0; i < M; ++i) {
                const bool even = ((pos[i].y.v[0] & 1ull) == 0);
                if (restart[i]) {
                    // Gaudry–Schost: teleport to a fresh uniformly random spot of the region.
                    restart[i] = 0;
                    const int r = (int)(rng() % TEL);
                    const U256 v = sc::add(sc::add(origin[i], dist[i]), tdist[r]);
                    const bool wrap = !sc::is_neg(sc::sub(v, (i < T) ? N2 : W2));   // v >= limit
                    Q[i] = wrap ? tpw[r] : tpt[r];
                    stepd[i] = wrap ? twdist[r] : tdist[r];
                    for (int k = 0; k < 4; ++k) hist[(size_t)k * M + i] = EMPTY;
                    stall[i] = 0;
                } else {
                    const uint64_t x0 = pos[i].x.v[0], x1 = pos[i].x.v[1];
                    const bool cyc = (x0 == hist[s2 + i]) || (x0 == hist[s4 + i]);   // 2- or 4-cycle
                    hist[s4 + i] = x0;
                    if (cyc) {
                        const uint32_t e = (uint32_t)((x1 >> 20) % ESC);
                        Q[i] = even ? ept[e] : nept[e]; stepd[i] = even ? edist[e] : nedist[e];
                        ++l_esc;
                    } else {
                        const uint32_t j = (uint32_t)(x1 % JMP);
                        Q[i] = even ? jpt[j] : njpt[j]; stepd[i] = even ? jdist[j] : njdist[j];
                    }
                }
                // Degenerate batch input (x equal; only plausible at tiny ranges): use another jump.
                for (int alt = 0; alt < ESC && pos[i].x.v[0] == Q[i].x.v[0] && ec::fe_eq(pos[i].x, Q[i].x); ++alt) {
                    Q[i] = even ? ept[alt] : nept[alt]; stepd[i] = even ? edist[alt] : nedist[alt];
                }
            }
            ec::point_add_batch(pos.data(), Q.data(), Rp.data(), M);
            for (int i = 0; i < M; ++i) { pos[i] = Rp[i]; dist[i] = sc::add(dist[i], stepd[i]); ++stall[i]; }
            ++t;
            local += M;
            if (local >= 0x10000) { gjumps.fetch_add(local, std::memory_order_relaxed); local = 0; }
            for (int i = 0; i < M; ++i) {
                if ((pos[i].x.v[0] & dpmask) != 0) {
                    if (stall[i] >= stall_limit) { restart[i] = 1; ++l_rst; }
                    continue;
                }
                restart[i] = 1;                     // end of trail: record the DP, then teleport
                const uint8_t type = (i < T) ? 0 : 1;
                const U256 val = sc::add(origin[i], dist[i]);
                std::lock_guard<std::mutex> lk(mtx);
                if (found.load()) break;
                auto it = table.find(xkey(pos[i]));
                if (it == table.end()) { table.emplace(xkey(pos[i]), std::make_pair(type, vpack(val))); continue; }
                if (it->second.first == type) { ++l_reh; continue; }
                ++l_col;
                const U256 vT = (type == 0) ? val : vunpack(it->second.second);
                const U256 vW = (type == 1) ? val : vunpack(it->second.second);
                const U256 cand[2] = { sc::sub(vT, vW), sc::neg(sc::add(vT, vW)) };
                for (int c = 0; c < 2 && !found.load(); ++c) {
                    const U256 k = sc::add(cand[c], N2);
                    if (!sc::in_range(k, range)) continue;
                    const Point chk = ec::mul_g(k);
                    if (ec::fe_eq(chk.x, target.x) && ec::fe_eq(chk.y, target.y)) { gkey = k; found.store(true); }
                }
            }
        }
        gjumps.fetch_add(local, std::memory_order_relaxed);
        a_esc.fetch_add(l_esc); a_rst.fetch_add(l_rst); a_col.fetch_add(l_col); a_reh.fetch_add(l_reh);
    };

    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker, t);
    for (auto& th : pool) th.join();
    writer_stop.store(true, std::memory_order_relaxed);
    if (writer.joinable()) writer.join();
    if (ck && !ck->path.empty()) snapshot_now();   // final durable snapshot
    auto t1 = std::chrono::high_resolution_clock::now();
    R.seconds = std::chrono::duration<double>(t1 - t0).count();
    R.found = found.load(); R.key = gkey; R.jumps = gjumps.load(); R.dp_count = table.size();
    R.K = (double)R.jumps / std::pow(2.0, range / 2.0);
    R.escapes = a_esc.load(); R.restarts = a_rst.load(); R.collisions = a_col.load(); R.rehits = a_reh.load();
    return R;
}

} // namespace kang
