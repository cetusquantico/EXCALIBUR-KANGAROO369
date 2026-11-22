// EXCALIBUR-KANGAROO369 — test_ckpt: Phase 5 checkpoint/resume.
//   A) Snapshot save/load round-trip with multi-limb (>2^64) DP values.
//   B) Integrity: a single flipped byte is detected (digest mismatch).
//   C) End-to-end: a run paused early writes a checkpoint; a resume reloads the DP
//      table and goes on to recover the correct key.
#include "ec.h"
#include "kangaroo.h"
#include "checkpoint.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <atomic>

using ec::U256;
static int fails = 0, checks = 0;
static bool eqU(const U256& a, const U256& b){ return a.v[0]==b.v[0]&&a.v[1]==b.v[1]&&a.v[2]==b.v[2]&&a.v[3]==b.v[3]; }
static void ck(bool c, const char* n){ checks++; if(!c){fails++; printf("  FAIL: %s\n",n);} else printf("  ok  : %s\n",n); }

static const char* PATH = "test_ckpt_tmp.ckpt";

int main() {
    ec::init();
    printf("== test_ckpt: Phase 5 checkpoint/resume ==\n");

    // ---------- A) round-trip with multi-limb DP values ----------
    printf("\n-- A) save/load round-trip (multi-limb) --\n");
    {
        kang::Snapshot s;
        s.range = 73; s.sym = 1; s.seed = 0xDEADBEEFCAFEULL; s.jumps = 123456789ULL;
        s.target = ec::mul_g(U256{{0xABCDEF0123456789ULL, 0x55ULL, 0, 0}});   // key > 2^64
        s.dps.push_back({ U256{{1,2,3,4}}, 0, U256{{0xFFFFFFFFFFFFFFFFULL, 0xAULL, 0xBULL, 0}} });
        s.dps.push_back({ U256{{9,8,7,6}}, 1, U256{{0, 0xC0FFEEULL, 0, 0x1ULL}} });
        ck(kang::ckpt_save(PATH, s), "save ok");

        kang::Snapshot t;
        int rc = kang::ckpt_load(PATH, t);
        ck(rc == 0, "load ok");
        ck(t.range == s.range && t.sym == s.sym && t.seed == s.seed && t.jumps == s.jumps, "scalar fields match");
        ck(ec::fe_eq(t.target.x, s.target.x) && ec::fe_eq(t.target.y, s.target.y), "target point matches");
        ck(t.dps.size() == 2, "dp count matches");
        bool dps_ok = t.dps.size()==2
            && t.dps[0].type==0 && eqU(t.dps[0].x,s.dps[0].x) && eqU(t.dps[0].val,s.dps[0].val)
            && t.dps[1].type==1 && eqU(t.dps[1].x,s.dps[1].x) && eqU(t.dps[1].val,s.dps[1].val);
        ck(dps_ok, "dp contents match (multi-limb vals)");
    }

    // ---------- B) tamper detection ----------
    printf("\n-- B) integrity: flipped byte is rejected --\n");
    {
        FILE* f = fopen(PATH, "r+b");
        ck(f != nullptr, "reopen checkpoint");
        if (f) { fseek(f, 20, SEEK_SET); int c = fgetc(f); fseek(f, 20, SEEK_SET); fputc(c ^ 0xFF, f); fclose(f); }
        kang::Snapshot t;
        int rc = kang::ckpt_load(PATH, t);
        ck(rc == -3, "corrupted file -> digest mismatch (-3)");
    }
    remove(PATH);

    // ---------- C) end-to-end: pause, then resume to the correct key ----------
    printf("\n-- C) pause + resume recovers the key --\n");
    {
        const int range = 36;
        const U256 secret = U256{{0x09A3C1F02BULL, 0, 0, 0}};   // in [0, 2^36)
        ec::Point target = ec::mul_g(secret);

        // Phase A: cap jumps very low so it stops early (and writes a checkpoint).
        kang::CkptOpts a;
        a.path = PATH; a.interval_sec = 0 /* no periodic thread; rely on final save */;
        a.resume = false; a.session_salt = 1; a.max_jumps = 1;
        kang::Result ra = kang::solve_sym(target, range, 0xABC, 2, &a);
        printf("  phase A: found=%d jumps=%llu\n", (int)ra.found, (unsigned long long)ra.jumps);

        kang::Snapshot chk;
        int rc = kang::ckpt_load(PATH, chk);
        ck(rc == 0, "phase A wrote a loadable checkpoint");
        ck(chk.dps.size() >= 1, "checkpoint carries >=1 DP of progress");
        ck(chk.range == range, "checkpoint range matches");

        // Phase B: resume (fresh salt) and run to completion.
        kang::CkptOpts b;
        b.path = PATH; b.interval_sec = 0; b.resume = true; b.session_salt = 2; b.max_jumps = 0;
        kang::Result rb = kang::solve_sym(target, range, 0xABC, 2, &b);
        printf("  phase B: found=%d key=0x%llx loaded_dps=%llu jumps=%llu\n",
               (int)rb.found, (unsigned long long)rb.key.v[0],
               (unsigned long long)b.loaded_dps, (unsigned long long)rb.jumps);
        ck(b.loaded_dps >= 1, "phase B reloaded the DP table");
        ck(rb.found, "phase B found the key after resume");
        ck(eqU(rb.key, secret), "recovered key == secret");
    }
    remove(PATH);

    printf("\n== %d/%d checks passed ==\n", checks - fails, checks);
    if (fails) { printf("RESULT: FAIL (%d)\n", fails); return 1; }
    printf("RESULT: PASS — checkpoint/resume works\n");
    return 0;
}
