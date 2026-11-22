// EXCALIBUR — unified front-end that dispatches by target type:
//   --pubkey HEX   (public key exposed)  -> KANGAROO solver (this project)
//   --address ADDR (no public key)       -> BRUTE force (exlottery / kh71), via exec
#include "ec.h"
#include "kangaroo.h"
#include "hash.h"
#include "scalar.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static ec::Point negp(const ec::Point& p) { return ec::Point{p.x, ec::fe_neg(p.y), p.inf}; }

static void usage() {
    printf(
"EXCALIBUR — unified puzzle dispatcher\n"
"Usage:\n"
"  excalibur --puzzle N (--pubkey HEX | --address ADDR) [options]\n\n"
"Dispatch:\n"
"  --pubkey HEX    public key exposed  -> KANGAROO (this solver)\n"
"  --address ADDR  only the address    -> BRUTE force (exlottery)\n\n"
"Kangaroo options:\n"
"  --range N       override search range in bits (default: puzzle interval [2^(N-1),2^N))\n"
"  --threads N     worker threads (default 4)\n"
"  --seed N        RNG seed\n"
"  --no-sym        disable +-P symmetry (use plain kangaroo)\n\n"
"Brute engine (with --address):\n"
"  --engine NAME   exlottery (default, volante) | kh71 (base key hunter)\n"
"  --first-allow .. --allow ..   (exlottery volante; ignored by kh71)\n"
"  --mode .. --found .. --threads ..\n"
"  --brute-args \"..\"            raw flags appended verbatim (e.g. kh71 --fixed on ..)\n"
"  --brute-bin PATH  exlottery path   --kh71-bin PATH  kh71 path\n\n"
"General:\n"
"  --dry-run       show the dispatch decision/plan, do not run\n"
"  -h, --help\n");
}

int main(int argc, char** argv) {
    int puzzle = 0, threads = 4, range = 0;
    uint64_t seed = 0x1234567;
    bool dry = false, sym = true;
    std::string pubkey, address, brute_bin = "/root/EXCALIBUR-LOTTERY369/build-opt/exlottery";
    std::string kh71_bin = "/root/excalibur369/build/kh71";
    std::string engine = "exlottery", brute_args;
    std::string first_allow, allow, mode, found_file;

    auto need = [&](int& i) -> std::string {
        if (i + 1 >= argc) { fprintf(stderr, "missing value after %s\n", argv[i]); exit(2); }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--puzzle") puzzle = atoi(need(i).c_str());
        else if (a == "--pubkey") pubkey = need(i);
        else if (a == "--address") address = need(i);
        else if (a == "--range") range = atoi(need(i).c_str());
        else if (a == "--threads") threads = atoi(need(i).c_str());
        else if (a == "--seed") seed = strtoull(need(i).c_str(), nullptr, 0);
        else if (a == "--no-sym") sym = false;
        else if (a == "--dry-run") dry = true;
        else if (a == "--first-allow") first_allow = need(i);
        else if (a == "--allow") allow = need(i);
        else if (a == "--mode") mode = need(i);
        else if (a == "--found") found_file = need(i);
        else if (a == "--brute-bin") brute_bin = need(i);
        else if (a == "--kh71-bin") kh71_bin = need(i);
        else if (a == "--engine") engine = need(i);
        else if (a == "--brute-args") brute_args = need(i);
        else { fprintf(stderr, "unknown arg: %s\n", a.c_str()); usage(); return 2; }
    }

    ec::init();

    // ---------------- KANGAROO path (pubkey exposed) ----------------
    if (!pubkey.empty()) {
        ec::Point P;
        if (pubkey.size() == 66) {
            P = ec::decompress(pubkey);
            if (P.inf) { fprintf(stderr, "bad compressed pubkey\n"); return 2; }
        } else if (pubkey.size() == 130 && pubkey.substr(0,2) == "04") {
            P.inf = false; P.x = ec::u256_from_hex(pubkey.substr(2,64)); P.y = ec::u256_from_hex(pubkey.substr(66,64));
        } else { fprintf(stderr, "pubkey must be 66-char (02/03) or 130-char (04) hex\n"); return 2; }
        if (!ec::is_on_curve(P)) { fprintf(stderr, "pubkey not on curve\n"); return 2; }

        ec::U256 lo{{0,0,0,0}}; int reff;
        if (range > 0) { reff = range; lo = ec::U256{{0,0,0,0}}; }
        else if (puzzle > 0) { reff = puzzle - 1; lo = sc::pow2(puzzle - 1); }   // interval [2^(N-1),2^N)
        else { fprintf(stderr, "need --puzzle N or --range N for the kangaroo\n"); return 2; }
        if (reff < 1 || reff > 160) { fprintf(stderr, "range must be 2..160 bits\n"); return 2; }

        printf("[dispatch] public key exposed -> KANGAROO%s\n", sym ? " (+-P symmetry)" : "");
        printf("  puzzle=%d  effective range=%d bits  interval=[2^%d, 2^%d)  threads=%d\n",
               puzzle, reff, puzzle ? puzzle-1 : 0, puzzle ? puzzle : reff, threads);
        {   // honest ETA / feasibility note
            double est = std::pow(2.0, reff / 2.0) * (sym ? 1.1 : 2.0);   // ~jumps
            double secs = est / 1.0e7;                                    // ~1e7 jumps/s (4 threads)
            const char* unit = "s"; double v = secs;
            if (v > 3.15e7) { v = secs / 3.15e7; unit = "anos"; }
            else if (v > 86400) { v = secs / 86400; unit = "dias"; }
            else if (v > 3600) { v = secs / 3600; unit = "h"; }
            else if (v > 60) { v = secs / 60; unit = "min"; }
            printf("  estimativa: ~%.1e saltos  ~%.1f %s neste i5", est, v, unit);
            if (reff > 64) printf("  <-- inviavel nesta CPU; feito pra CPU mais rapida / futura GPU (Ctrl-C a qualquer hora)");
            printf("\n");
        }
        if (dry) { printf("  (dry-run) would solve now.\n"); return 0; }

        ec::Point Pshift = sc::is_zero(lo) ? P : ec::point_add(P, negp(ec::mul_g(lo)));
        kang::Result r = sym ? kang::solve_sym(Pshift, reff, seed, threads)
                             : kang::solve(Pshift, reff, seed, threads);
        if (!r.found) {
            printf("  NOT FOUND within cap (jumps=%llu, %.1fs). Try more threads/seed or larger cap.\n",
                   (unsigned long long)r.jumps, r.seconds);
            return 1;
        }
        ec::U256 k = sc::add(r.key, lo);
        std::vector<uint8_t> key32(32, 0);
        for (int j = 0; j < 32; ++j) key32[31 - j] = (uint8_t)(k.v[j / 8] >> (8 * (j % 8)));
        ec::Point chk = ec::mul_g(k);
        std::string got_pub = ec::compress(chk);
        std::string addr = hsh::p2pkh_address(got_pub);
        bool ok = ec::fe_eq(chk.x, P.x) && ec::fe_eq(chk.y, P.y);

        printf("\n  *** KEY FOUND *** %s\n", ok ? "(verified)" : "(!! verify FAILED)");
        printf("  private (hex) : %s\n", ec::fe_to_hex(k).c_str());
        if (k.v[1] == 0 && k.v[2] == 0 && k.v[3] == 0)
            printf("  private (dec) : %llu\n", (unsigned long long)k.v[0]);
        printf("  WIF           : %s\n", hsh::wif_compressed(key32).c_str());
        printf("  address       : %s\n", addr.c_str());
        if (!address.empty())
            printf("  matches --address : %s\n", (addr == address) ? "YES" : "NO");
        printf("  jumps=%llu  K=%.2f  threads=%d  time=%.2fs\n",
               (unsigned long long)r.jumps, r.K, r.threads, r.seconds);
        return ok ? 0 : 4;
    }

    // ---------------- BRUTE path (only an address) ----------------
    if (!address.empty()) {
        if (puzzle <= 0) { fprintf(stderr, "need --puzzle N for brute\n"); return 2; }
        bool is_kh71 = (engine == "kh71");
        if (engine != "kh71" && engine != "exlottery") {
            fprintf(stderr, "unknown --engine '%s' (use exlottery | kh71)\n", engine.c_str()); return 2;
        }
        const std::string bin = is_kh71 ? kh71_bin : brute_bin;
        std::string cmd = bin + " --puzzle " + std::to_string(puzzle) + " --address " + address;

        if (is_kh71) {
            // kh71: base key hunter. Volante allow-lists are exlottery-only.
            if (!first_allow.empty() || !allow.empty())
                fprintf(stderr, "  note: --first-allow/--allow are exlottery-only; "
                                "for kh71 use --brute-args \"--fixed on --fixed-count N ...\"\n");
            if (!mode.empty())       cmd += " --mode " + mode;
            cmd += " --threads " + std::to_string(threads);
            if (!found_file.empty()) cmd += " --output " + found_file;   // kh71 uses --output
        } else {
            cmd += " --search";
            if (!first_allow.empty()) cmd += " --first-allow " + first_allow;
            if (!allow.empty())       cmd += " --allow " + allow;
            if (!mode.empty())        cmd += " --mode " + mode;
            cmd += " --threads " + std::to_string(threads);
            if (!found_file.empty())  cmd += " --found " + found_file;
        }
        if (!brute_args.empty()) cmd += " " + brute_args;

        printf("[dispatch] no public key -> BRUTE force (%s)\n", is_kh71 ? "kh71 / base key hunter" : "exlottery / volante");
        printf("  binary : %s\n", bin.c_str());
        printf("  command: %s\n", cmd.c_str());
        if (dry) { printf("  (dry-run) not executed.\n"); return 0; }
        FILE* t = fopen(bin.c_str(), "rb");
        if (!t) { fprintf(stderr, "  engine binary not found; use --brute-bin / --kh71-bin PATH\n"); return 2; }
        fclose(t);
        return system(cmd.c_str());
    }

    usage();
    return 2;
}
