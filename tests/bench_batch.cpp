// Phase 1: batch inversion benchmark + correctness.
// Compares naive point-add (1 Fermat inversion each) vs batched point-add
// (1 inversion per batch). This is the kangaroo "jump" hot path.
#include "../src/ec.h"
#include <cstdio>
#include <vector>
#include <chrono>

using ec::Point; using ec::U256;

int main() {
    ec::init();
    const int N = 1024;

    // Build N kangaroo-like points P[i] = k_i*G and addends Q[i] = j_i*G,
    // with distinct x (jump points). One-time setup (uses slow mul_g).
    printf("Setup: generating %d points...\n", N);
    std::vector<Point> P(N), Q(N), Rn(N), Rb(N);
    for (int i = 0; i < N; ++i) {
        U256 k{{(uint64_t)(0x9e3779b97f4a7c15ULL * (i + 1)), (uint64_t)(i + 1), 0, 0}};
        U256 j{{(uint64_t)(0xC2B2AE3D27D4EB4FULL * (i + 7)), (uint64_t)(i + 100), 0, 0}};
        P[i] = ec::mul_g(k);
        Q[i] = ec::mul_g(j);
    }

    // --- correctness: batched must equal naive ---
    ec::point_add_batch(P.data(), Q.data(), Rb.data(), N);
    int mism = 0;
    for (int i = 0; i < N; ++i) {
        Point r = ec::point_add(P[i], Q[i]);
        if (!(ec::fe_eq(r.x, Rb[i].x) && ec::fe_eq(r.y, Rb[i].y))) ++mism;
    }
    printf("Correctness: batched vs naive over %d adds -> %s (%d mismatches)\n",
           N, mism == 0 ? "OK" : "FAIL", mism);

    volatile uint64_t sink = 0;

    // --- naive timing ---
    const int ITER_NAIVE = 300;            // ITER*N adds
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < ITER_NAIVE; ++it)
        for (int i = 0; i < N; ++i) { Point r = ec::point_add(P[i], Q[i]); sink ^= r.x.v[0]; }
    auto t1 = std::chrono::high_resolution_clock::now();
    double sec_n = std::chrono::duration<double>(t1 - t0).count();
    double adds_n = (double)ITER_NAIVE * N;
    double rate_n = adds_n / sec_n;

    // --- batched timing ---
    const int ITER_BATCH = 4000;
    t0 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < ITER_BATCH; ++it) {
        ec::point_add_batch(P.data(), Q.data(), Rn.data(), N);
        sink ^= Rn[0].x.v[0];
    }
    t1 = std::chrono::high_resolution_clock::now();
    double sec_b = std::chrono::duration<double>(t1 - t0).count();
    double adds_b = (double)ITER_BATCH * N;
    double rate_b = adds_b / sec_b;

    printf("\n== Point-add throughput (1 thread) ==\n");
    printf("  naive   : %10.0f adds/s\n", rate_n);
    printf("  batched : %10.0f adds/s   (%.1fx faster)\n", rate_b, rate_b / rate_n);
    printf("\n== Kangaroo jump-rate projection ==\n");
    printf("  1 thread  : ~%.2f M jumps/s\n", rate_b / 1e6);
    printf("  4 threads : ~%.2f M jumps/s\n", 4 * rate_b / 1e6);
    double R48 = 1.15 * 16777216.0;        // ops for 48-bit range
    printf("  -> synthetic 48-bit solve (~%.1fM ops) ~ %.1f s on 4 threads\n",
           R48 / 1e6, R48 / (4 * rate_b));
    (void)sink;
    return mism ? 1 : 0;
}
