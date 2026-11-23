# Security and intended use

## Intended use

EXCALIBUR-KANGAROO369 is an educational, clean-room implementation of Pollard's kangaroo
for secp256k1. Its supported purpose is participation in the **intentionally public
Bitcoin puzzle challenge** and the study of interval discrete-logarithm algorithms.
Use it only on keys you own or on targets that were published to be solved.

Real wallets use full 256-bit keys; the methods here are only practical on the small
intervals that the puzzle challenge deliberately exposes.

## Reporting a problem

For a correctness issue, open an issue with a synthetic or solved, public fixture, the
exact command, the expected and actual output, your CPU and your compiler version.

For a suspected security issue, use GitHub private vulnerability reporting if it is
enabled for this repository; otherwise ask the maintainer for a private contact without
posting sensitive details publicly.

**Never attach** wallet seeds, unpublished private keys, credentials or personal
checkpoint files to an issue.

## Checkpoint files

Checkpoint files (`*.ckpt`) hold distinguished points with their scalar distances in
clear, so treat them as sensitive: they are written with mode `0600` and are ignored by
`.gitignore`. The trailing SHA-256 digest detects accidental corruption; it does **not**
authenticate a file received from someone else. Keep each checkpoint under one running
process.
