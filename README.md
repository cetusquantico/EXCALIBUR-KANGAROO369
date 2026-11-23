# EXCALIBUR-KANGAROO369

*[Versão em português: LEIAME.md](LEIAME.md)*

A clean-room **Pollard's kangaroo** solver for the secp256k1 discrete logarithm in an
interval, written from scratch in C++17. It targets the public **Bitcoin puzzle
challenge** in the case where a puzzle's **public key is exposed**: the kangaroo then
recovers the private key in about `K·√W` group operations instead of brute-forcing all
`W` keys of the interval.

It is a CPU reference implementation — correct, tested and documented — meant to be the
base for a future GPU port, not a speed record.

> **Clean-room:** the elliptic-curve arithmetic is implemented from the standard
> formulas; no GPL code was copied (in particular, none from RCKangaroo).

## Result on real data

Puzzle **68** (already solved publicly) recovered from its exposed public key, through
the CLI, with checkpointing enabled:

```
private (hex)     : ...0bebb3940cd0fc1491   (matches the published solution)
matches --address : YES
jumps=2.04e10  K=1.68  threads=2  time=2996s       (Intel i5-3470, ~50 min)
```

## Features

- **secp256k1 from scratch:** field arithmetic, affine point addition/doubling, Fermat
  inversion, scalar multiplication, point (de)compression — checked against the public
  keys of the solved puzzles.
- **Batch inversion** (Montgomery's trick) in the hot loop: one inversion per batch of
  points, ~64× cheaper than independent additions; ~3.4 M jumps/s per core on an Ivy
  Bridge i5.
- **Two solvers:** a plain two-herd van Oorschot–Wiener kangaroo (`solve`) and the default
  **±P negation-map** solver implemented as **Gaudry–Schost walks** (`solve_sym`).
- **Multithreaded**, with a shared distinguished-point (DP) table.
- **256-bit distance tracking**, so ranges from **2 to 160 bits** are handled correctly —
  time, not integer width, is the only limit.
- **Checkpoint / resume** of the DP table: atomic write (`tmp` + `fsync` + `rename`, mode
  `0600`), trailing SHA-256 digest, final snapshot on `Ctrl+C`.
- **Unified CLI** (`excalibur`): prints the key in hex, as a compressed WIF and as the
  derived address, and verifies it against `--address`.

## Why the negation map needed Gaudry–Schost walks

The first symmetric version chose the jump sign from the parity of `y`. That makes each
step a function of the class `{P, −P}`, as the negation map requires — but it also turns
the kangaroo into a **symmetric one-dimensional random walk** in `|dlog|`. Such a walk over
a finite set of jumps is recurrent: after roughly `σ^(2/3)` steps (`σ` ≈ jump size) it
returns to an *exact* earlier point and closes a long loop that keeps re-hitting known
distinguished points. That time grows more slowly than the spacing between distinguished
points (`2^dp`), so the larger the range, the worse it got.

Measured at 54 bits with instrumentation: more same-herd re-hits than new DPs, **zero**
tame/wild collisions, and no solution even after `16·√W` jumps.

The fix uses the negation map the standard way for an interval:

1. after **every** distinguished point a walk restarts at a fresh, uniformly random point
   of its herd's region — a precomputed random "teleport" `t·G` folded into the next batch
   addition, so a restart costs one batched addition and no scalar multiplication;
2. the trail length `2^dp` is kept well below the self-intersection time (about 1% of
   trails close a loop);
3. 2- and 4-cycles are escaped with a deterministic, class-invariant jump taken from a
   separate table of larger distances, so the escape cannot fall back into the cycle;
4. as a backstop, a walk that goes `8·2^dp` steps without a DP is teleported as well.

| range (bits) | first symmetric version | Gaudry–Schost | K | re-hits | time (2 threads) |
|---|---|---|---|---|---|
| 52 | unstable (K up to 7) | ✅ correct key | 1.99 | 1 | 20 s |
| 54 | ❌ not even at 16·√W | ✅ correct key | 1.91 | 3 | 38 s |
| 56 | ❌ | ✅ correct key | 1.22 | 0 | 49 s |
| 58 | ❌ | ✅ correct key | 3.95 | 7 | 312 s |
| 60 | ❌ | ✅ correct key | 1.41 | 3 | 224 s |
| 67 (puzzle 68) | — | ✅ correct key | 1.68 | — | ~50 min |

The measured mean is K ≈ 2 (single runs vary a lot). Today the symmetric solver ties the
plain one on jumps, but it is the one with checkpoint/resume; getting closer to the
theoretical ~1.36 of Gaudry–Schost with the negation map is a matter of tuning the
regions (future work). The `symmetry_54bit` test guards against the regression.

## Build and test

Requires a C++17 compiler (g++ or clang++), CMake ≥ 3.10 and pthreads — no other
dependencies.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

The Release build uses `-march=native`, so the binary is tuned for (and only guaranteed
to run on) the machine that built it.

| test | what it checks |
|---|---|
| `ec_vectors` | curve arithmetic; `mul_g(k)` against the public keys of puzzles 95–135; decompression |
| `batch_correctness` | batched additions equal independent additions |
| `kangaroo_solve` | plain kangaroo on synthetic ranges |
| `kangaroo_symmetry` | plain vs. symmetric solver at 40/44/48 bits |
| `real_puzzles` | every row of `tests/puzzles.csv` (`mul_g(priv) == pubkey`, address), then solves real puzzles 40/44/48 from their public keys |
| `wide_160bit` | multi-limb arithmetic and key recovery above 2^64, up to 160 bits |
| `checkpoint_resume` | save/load round-trip, tamper detection, pause → resume → correct key |
| `symmetry_54bit` | solves a 54-bit case the first symmetric version never could, and fails if the trap signature (re-hits) comes back |

`tests/puzzles.csv` lists the publicly **solved** puzzles (their keys are already public)
and is used as known-answer data.

## Usage

```bash
# exposed public key -> kangaroo over the exact puzzle interval [2^(N-1), 2^N)
./build/excalibur --puzzle 68 --pubkey <COMPRESSED_PUBKEY_HEX> --address <P2PKH_ADDRESS> \
    --threads 2 --checkpoint p68.ckpt --checkpoint-interval 30

# resume after a pause or a reboot
./build/excalibur --puzzle 68 --pubkey <...> --address <...> --threads 2 --resume p68.ckpt

# only print the plan and a time estimate
./build/excalibur --puzzle 68 --pubkey <...> --address <...> --dry-run
```

Other options: `--range N` (custom range in bits), `--threads N`, `--seed N` and
`--no-sym` (plain kangaroo; checkpointing is not available in that mode). The CLI prints
an honest time estimate and warns when a run is out of reach for the machine.

**Address-only targets.** Without a public key the kangaroo cannot be used, so the CLI
hands the target to an external brute-force engine (`exlottery` by default, or
`--engine kh71`). Those engines are **separate tools and are not part of this
repository**; the CLI looks for them next to its own binary, then on `PATH`, or takes
`--brute-bin` / `--kh71-bin`. `--first-allow`, `--allow`, `--mode`, `--found` and
`--brute-args` are passed through to the engine. For `exlottery`, the dispatcher also
exposes `--volante NAME` (a panel-saved wheel, used instead of `--puzzle`),
`--stop-on-hit`, `--quiet` and `--output human|kv` as first-class flags.

## Limits

This runs on a CPU. The puzzles that remain unsolved with an exposed public key are far
beyond CPU reach, and even a single GPU competes against large pools. The value here is a
correct, tested and documented reference that a GPU kernel can mirror.

## Intended use

For the intentionally public Bitcoin puzzle challenge and for studying interval
discrete-logarithm algorithms. Use it only on keys you own or on targets published to be
solved. Checkpoint files contain scalars in clear — keep them private. See
[SECURITY.md](SECURITY.md).

## Support

Self-funded, spare-time work. The next big step — a GPU port of the kangaroo — needs
hardware. If this is useful to you and you'd like to help, Bitcoin donations are welcome,
with no promise of anything in return:

`bc1qhsq406jt6ghkzmhpnqe4r2s639t6mtrhj6zx03`

## License

[MIT](LICENSE).
