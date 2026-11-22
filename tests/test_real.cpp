// Real-data validation using the solved-puzzle fixture (tests/puzzles.csv):
//   A) EC math: mul_g(priv) must compress to the published pubkey, for EVERY row.
//   B) Kangaroo: solve a few low-range REAL puzzles from their exposed pubkeys and
//      confirm we recover the published private key (not a synthetic one).
#include "../src/ec.h"
#include "../src/kangaroo.h"
#include "../src/hash.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Row { int puzzle; std::string priv, pub, addr; };

static std::ifstream open_csv(int argc, char** argv) {
    std::vector<std::string> cands;
    if (argc > 1) cands.push_back(argv[1]);
    cands.insert(cands.end(), {"puzzles.csv", "build/puzzles.csv", "tests/puzzles.csv"});
    for (auto& p : cands) { std::ifstream f(p); if (f) { printf("fixture: %s\n", p.c_str()); return f; } }
    return std::ifstream();
}

int main(int argc, char** argv) {
    ec::init();
    std::ifstream f = open_csv(argc, argv);
    if (!f) { printf("ERROR: puzzles.csv not found\n"); return 2; }

    std::vector<Row> rows;
    std::string line; bool header = true;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (header) { header = false; continue; }                 // skip CSV header
        std::stringstream ss(line); std::string c[5];
        for (int i = 0; i < 5 && std::getline(ss, c[i], ','); ++i) {}
        if (c[1].empty() || c[4].empty()) continue;
        rows.push_back({atoi(c[0].c_str()), c[1], c[4], c[3]});
    }
    printf("loaded %zu puzzle rows\n\n", rows.size());

    int pass = 0, fail = 0;

    // --- A) EC validation over ALL rows ---
    printf("== A) mul_g(priv) == published pubkey (all rows) ==\n");
    int bad = 0;
    for (auto& r : rows) {
        ec::Point P = ec::mul_g(ec::u256_from_hex(r.priv));
        if (ec::compress(P) != r.pub) { bad++; printf("  MISMATCH puzzle %d\n", r.puzzle); }
    }
    printf("  %zu/%zu rows OK\n", rows.size() - bad, rows.size());
    (bad == 0) ? ++pass : ++fail;

    // --- C) hash stack: address derived from pubkey == published address ---
    printf("\n== C) p2pkh_address(pubkey) == published address (all rows) ==\n");
    int badc = 0;
    for (auto& r : rows) if (hsh::p2pkh_address(r.pub) != r.addr) { badc++; printf("  MISMATCH puzzle %d\n", r.puzzle); }
    printf("  %zu/%zu addresses OK\n", rows.size() - badc, rows.size());
    (badc == 0) ? ++pass : ++fail;

    // --- B) Kangaroo on REAL low-range puzzles (feasible on CPU) ---
    printf("\n== B) kangaroo (symmetry) on real exposed-pubkey puzzles ==\n");
    for (int want : {40, 44, 48}) {
        const Row* r = nullptr;
        for (auto& x : rows) if (x.puzzle == want) { r = &x; break; }
        if (!r) { printf("  puzzle %d not in fixture\n", want); continue; }
        ec::Point P = ec::decompress(r->pub);
        uint64_t truek = ec::u256_from_hex(r->priv).v[0];
        kang::Result res = kang::solve_sym(P, want, 0xA11CE ^ truek, 4);
        bool ok = res.found && res.key.v[0] == truek && res.key.v[1] == 0 && res.key.v[2] == 0 && res.key.v[3] == 0;
        printf("  puzzle %2d : %-5s recovered=0x%llx (real=0x%llx)  K=%.2f  %.2fs\n",
               want, ok ? "MATCH" : "WRONG",
               (unsigned long long)res.key.v[0], (unsigned long long)truek, res.K, res.seconds);
        ok ? ++pass : ++fail;
    }

    printf("\n== RESULT: %d passed, %d failed ==\n", pass, fail);
    return fail ? 1 : 0;
}
