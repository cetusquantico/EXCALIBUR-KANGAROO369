// EXCALIBUR — unified front-end that dispatches by target type:
//   --pubkey HEX   (public key exposed)  -> KANGAROO solver (this project)
//   --address ADDR (no public key)       -> BRUTE force (exlottery / kh71), via exec
#include "ec.h"
#include "kangaroo.h"
#include "hash.h"
#include "scalar.h"
#include "checkpoint.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <csignal>
#include <atomic>
#include <chrono>
#include <unistd.h>

static ec::Point negp(const ec::Point& p) { return ec::Point{p.x, ec::fe_neg(p.y), p.inf}; }

static std::atomic<bool> g_stop{false};
static void on_sigint(int) { g_stop.store(true); }

// Locate an external brute engine: next to this executable first, then on PATH.
// Returns "" when not found (the caller reports how to provide it).
static std::string find_engine(const std::string& name) {
    char self[4096];
    const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = '\0';
        const std::string exe(self);
        const size_t slash = exe.rfind('/');
        if (slash != std::string::npos) {
            const std::string cand = exe.substr(0, slash + 1) + name;
            if (access(cand.c_str(), X_OK) == 0) return cand;
        }
    }
    if (const char* path = getenv("PATH")) {
        const std::string p(path);
        for (size_t start = 0; start <= p.size();) {
            size_t end = p.find(':', start);
            if (end == std::string::npos) end = p.size();
            const std::string dir = p.substr(start, end - start);
            if (!dir.empty()) {
                const std::string cand = dir + "/" + name;
                if (access(cand.c_str(), X_OK) == 0) return cand;
            }
            start = end + 1;
        }
    }
    return "";
}

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
"  --no-sym        disable +-P symmetry (use plain kangaroo)\n"
"  --checkpoint F  save DP-table progress to F periodically (atomic, sha256)\n"
"  --resume F      resume a kangaroo run from checkpoint F\n"
"  --checkpoint-interval S   seconds between snapshots (default 30)\n"
"  (Ctrl+C writes a final snapshot and exits; checkpointing needs symmetry mode)\n\n"
"Brute engine (with --address):\n"
"  --engine NAME   exlottery (default, volante) | kh71 (base key hunter)\n"
"  --first-allow .. --allow ..   (exlottery volante; ignored by kh71)\n"
"  --volante NAME  exlottery: use a panel-saved volante (puzzle + both lists)\n"
"  --stop-on-hit   exlottery: stop at the first confirmed key\n"
"  --quiet         exlottery: hide the plan and progress\n"
"  --output human|kv   exlottery: result format (kv for scripts)\n"
"  --mode .. --found .. --threads ..\n"
"  --brute-args \"..\"            raw flags appended verbatim (e.g. kh71 --fixed on ..)\n"
"  --brute-bin PATH  exlottery path   --kh71-bin PATH  kh71 path\n"
"                  (default: looked up next to this binary, then on PATH;\n"
"                   the brute engines are separate tools, not part of this repo)\n\n"
"General:\n"
"  --dry-run       show the dispatch decision/plan, do not run\n"
"  -h, --help\n");
}

int main(int argc, char** argv) {
    int puzzle = 0, threads = 4, range = 0;
    uint64_t seed = 0x1234567;
    bool dry = false, sym = true;
    std::string pubkey, address, brute_bin, kh71_bin;   // empty = auto-locate (find_engine)
    std::string engine = "exlottery", brute_args;
    std::string first_allow, allow, mode, found_file;
    std::string volante, out_format; bool stop_on_hit = false, quiet = false;   // exlottery
    std::string ckpt_path, resume_path; int ckpt_interval = 30;

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
        else if (a == "--checkpoint") ckpt_path = need(i);
        else if (a == "--resume") resume_path = need(i);
        else if (a == "--checkpoint-interval") ckpt_interval = atoi(need(i).c_str());
        else if (a == "--dry-run") dry = true;
        else if (a == "--first-allow") first_allow = need(i);
        else if (a == "--allow") allow = need(i);
        else if (a == "--mode") mode = need(i);
        else if (a == "--found") found_file = need(i);
        else if (a == "--brute-bin") brute_bin = need(i);
        else if (a == "--kh71-bin") kh71_bin = need(i);
        else if (a == "--engine") engine = need(i);
        else if (a == "--brute-args") brute_args = need(i);
        else if (a == "--volante") volante = need(i);
        else if (a == "--stop-on-hit") stop_on_hit = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--output") out_format = need(i);
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
            double est = std::pow(2.0, reff / 2.0) * (sym ? 2.0 : 2.3);   // ~jumps (measured mean K)
            int tc = threads < 1 ? 1 : (threads > 4 ? 4 : threads);       // i5-3470: 4 cores
            double secs = est / (3.2e6 * tc);                             // ~3.2M jumps/s per core
            const char* unit = "s"; double v = secs;
            if (v > 3.15e7) { v = secs / 3.15e7; unit = "anos"; }
            else if (v > 86400) { v = secs / 86400; unit = "dias"; }
            else if (v > 3600) { v = secs / 3600; unit = "h"; }
            else if (v > 60) { v = secs / 60; unit = "min"; }
            printf("  estimativa: ~%.1e saltos  ~%.1f %s neste i5", est, v, unit);
            if (secs > 30.0 * 86400) printf("  <-- inviavel nesta CPU; feito pra CPU mais rapida / futura GPU (Ctrl-C a qualquer hora)");
            printf("\n");
        }
        // Phase 5: checkpoint/resume (symmetry mode only)
        kang::CkptOpts ck;
        std::string cpath = !resume_path.empty() ? resume_path : ckpt_path;
        kang::CkptOpts* ckp = nullptr;
        if (!cpath.empty()) {
            if (!sym) {
                fprintf(stderr, "  note: checkpoint/resume needs symmetry mode; ignoring (remove --no-sym)\n");
            } else {
                ck.path = cpath;
                ck.interval_sec = ckpt_interval;
                ck.resume = !resume_path.empty();
                ck.session_salt = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
                ck.stop = &g_stop;
                ckp = &ck;
                signal(SIGINT, on_sigint);
                printf("  checkpoint: %s%s (every %ds)\n", ck.path.c_str(),
                       ck.resume ? " [resuming]" : "", ck.interval_sec);
                if (ck.resume) {   // say up front whether the resume will actually take
                    kang::Snapshot s; int rc = kang::ckpt_load(ck.path, s);
                    if (rc != 0)
                        printf("  note: cannot load %s (%s) - starting FRESH\n", ck.path.c_str(),
                               rc == -1 ? "missing/unreadable" : rc == -3 ? "corrupt: digest mismatch"
                                        : "bad format or older walk version");
                    else if (s.range != reff)
                        printf("  note: %s is for range %d, not %d - starting FRESH\n",
                               ck.path.c_str(), s.range, reff);
                    else
                        printf("  resume: %zu DPs, %llu jumps already done\n",
                               s.dps.size(), (unsigned long long)s.jumps);
                }
            }
        }
        if (dry) { printf("  (dry-run) would solve now.\n"); return 0; }

        ec::Point Pshift = sc::is_zero(lo) ? P : ec::point_add(P, negp(ec::mul_g(lo)));
        kang::Result r = sym ? kang::solve_sym(Pshift, reff, seed, threads, ckp)
                             : kang::solve(Pshift, reff, seed, threads);
        if (ckp && ck.resume && ck.loaded_dps)
            printf("  resumed %llu distinguished points from checkpoint\n", (unsigned long long)ck.loaded_dps);
        if (!r.found) {
            if (ckp && g_stop.load()) {
                printf("  PAUSED — progress saved to %s (%llu DPs, jumps=%llu). Resume with: --resume %s\n",
                       ck.path.c_str(), (unsigned long long)r.dp_count,
                       (unsigned long long)r.jumps, ck.path.c_str());
                return 0;
            }
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
        bool is_kh71 = (engine == "kh71");
        if (engine != "kh71" && engine != "exlottery") {
            fprintf(stderr, "unknown --engine '%s' (use exlottery | kh71)\n", engine.c_str()); return 2;
        }
        if (!volante.empty() && is_kh71) { fprintf(stderr, "--volante is exlottery-only\n"); return 2; }
        if (puzzle <= 0 && volante.empty()) {
            fprintf(stderr, "need --puzzle N (or --volante NAME for exlottery) for brute\n"); return 2;
        }
        const char* ename = is_kh71 ? "kh71" : "exlottery";
        std::string bin = is_kh71 ? kh71_bin : brute_bin;
        if (bin.empty()) bin = find_engine(ename);
        const bool have_bin = !bin.empty() && access(bin.c_str(), X_OK) == 0;
        std::string cmd = "'" + (bin.empty() ? std::string(ename) : bin) + "'";

        if (is_kh71) {
            cmd += " --puzzle " + std::to_string(puzzle) + " --address " + address;
            // kh71: base key hunter. Volante allow-lists are exlottery-only.
            if (!first_allow.empty() || !allow.empty())
                fprintf(stderr, "  note: --first-allow/--allow are exlottery-only; "
                                "for kh71 use --brute-args \"--fixed on --fixed-count N ...\"\n");
            if (!mode.empty())       cmd += " --mode " + mode;
            cmd += " --threads " + std::to_string(threads);
            if (!found_file.empty()) cmd += " --output " + found_file;   // kh71 uses --output
            if (stop_on_hit || quiet || !out_format.empty() || !volante.empty())
                fprintf(stderr, "  note: --volante/--stop-on-hit/--quiet/--output are exlottery-only (ignored for kh71)\n");
        } else {
            // exlottery: a saved volante supplies the puzzle + both byte lists; otherwise --puzzle.
            if (!volante.empty()) cmd += " --volante " + volante;
            else {
                cmd += " --puzzle " + std::to_string(puzzle);
                if (!first_allow.empty()) cmd += " --first-allow " + first_allow;
                if (!allow.empty())       cmd += " --allow " + allow;
            }
            cmd += " --search --address " + address;
            if (!mode.empty())        cmd += " --mode " + mode;
            cmd += " --threads " + std::to_string(threads);
            if (!found_file.empty())  cmd += " --found " + found_file;
            if (stop_on_hit)          cmd += " --stop-on-hit";
            if (quiet)                cmd += " --quiet";
            if (!out_format.empty())  cmd += " --output " + out_format;
        }
        if (!brute_args.empty()) cmd += " " + brute_args;

        printf("[dispatch] no public key -> BRUTE force (%s)\n", is_kh71 ? "kh71 / base key hunter" : "exlottery / volante");
        printf("  binary : %s\n", have_bin ? bin.c_str() : "(not found)");
        printf("  command: %s\n", cmd.c_str());
        if (!have_bin)
            fprintf(stderr, "  engine '%s' not found: put it next to excalibur or on PATH, or pass %s PATH\n",
                    ename, is_kh71 ? "--kh71-bin" : "--brute-bin");
        if (dry) { printf("  (dry-run) not executed.\n"); return 0; }
        if (!have_bin) return 2;
        return system(cmd.c_str());
    }

    usage();
    return 2;
}
