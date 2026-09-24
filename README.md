# Opus 5.5 chess 24hrs

A UCI chess engine written from scratch, in one 24-hour session, by an AI model (**Opus 5.5**, Anthropic),
working fully autonomously. The final engine is a bitboard/PEXT alpha-beta searcher with an NNUE evaluation
trained entirely on self-play data generated during the same 24 hours.

**Final deliverable:** `final/Opus55chess24hrs.exe` (Windows x86-64, AVX2/BMI2, standalone).
**Best strength estimate:** ≈ **3500 ± 40** on the CCRL-blitz scale at 10+0.1. This is an estimate from matches
against the Stash calibration engines on one machine; see [Estimated strength](#estimated-strength).
**Official rating:** **3463 Elo ±16** (95% CI, CCRL Blitz scale), first of six AI engines in the series;
see [Official results](#official-results).

---

## What this is

This repository is one run of the *24-Hour Chess Engine Benchmark*. The full rules are in [`CLAUDE.md`](CLAUDE.md).
In short:

- One model (here Opus 5.5) gets **24 hours of wall-clock time**. That covers all thinking, coding, testing,
  training and writing.
- It must write a **UCI chess engine from scratch** in C or C++, using only the standard library and compiler intrinsics.
  Public *documentation* may be used, such as chessprogramming.org and papers. It may not copy source code from existing
  engines or use any pre-existing network, opening book or tablebase file. Values the model derives itself, including
  networks trained on its own games, are allowed.
- The only score is the **playing strength (Elo)** of the executable left in `final/`. It is rated with fastchess
  at **10+0.1**, single thread, Hash 256 MB, using the UHO opening book, against Stash versions and other engines.
- Time forfeits, crashes, illegal moves and hangs count as losses. The target is a modern x86-64 Windows 11 laptop
  (AVX2, BMI2, fast PEXT; no AVX-512), and the executable must be fully standalone.
- The model must log its progress hourly (`docs/progress.md`) and all internet use (`docs/resources.md`), and keep
  snapshots so it can roll back (`source/snapshots/`).

The run started **2026-09-22 16:15:36 −04:00** and ended **2026-09-23 16:15:36 −04:00**. The final build was frozen at 14:52.

## The engine

| | |
|---|---|
| Name (`id name`) | `Opus 5.5 chess 24hrs` |
| Author (`id author`) | `Opus 5.5` |
| Executable | `final/Opus55chess24hrs.exe` (≈3.7 MB; the network is embedded) |
| Language | C++17 |
| Compiler | GCC 15.2.0 (MinGW-w64, via scoop) on Windows 11 |
| Code size | ≈3,200 lines (engine + trainer) |

**Why C++17:** performance comparable to C, plus convenient standard-library threads, atomics, `std::chrono` and
containers. These were needed for the UCI input thread, the data generator and the multi-threaded NNUE trainer.
Intrinsics (`_pext_u64`, AVX2) come straight from GCC.

### Building

From `source/`, in a MinGW/Git-bash shell:

```bash
# final engine (net embedded via .incbin)
EXTRA_FLAGS="-DEMBED_NET=nets/net13.nnue" bash build.sh build/Opus55chess24hrs.exe
```

`build.sh` runs:

```
g++ -std=c++17 -O3 -march=x86-64-v3 -mtune=znver4 -flto -flto-partition=one -DNDEBUG -static -fno-exceptions \
    -DEMBED_NET=nets/net13.nnue -o <out> src/main.cpp src/position.cpp src/bitboard.cpp src/search.cpp \
    src/eval.cpp src/perft.cpp src/bench.cpp src/datagen.cpp src/nnue.cpp -lpthread
```

The build targets `x86-64-v3` (AVX2/BMI2/POPCNT) and is linked statically, so it depends only on `KERNEL32.dll` and
`msvcrt.dll`. The NNUE trainer is built separately:
`g++ -O3 -march=native -std=c++17 -static -o build/train.exe trainer/train.cpp`.
The final network file is `source/nets/net13.nnue`; a copy is also in the final snapshot.

### Running

It's a normal UCI engine: point any UCI GUI or fastchess at the executable. There are also extra command-line modes:

```
Opus55chess24hrs.exe bench [depth]                  # fixed-depth node-count benchmark
Opus55chess24hrs.exe perftsuite <perft.epd> [depth]  # move-generator verification
Opus55chess24hrs.exe datagen <out.bin> <games> <nodes> <seed>   # self-play training data
```

Non-standard interactive commands: `d` (print FEN), `eval`, `perft N`, `loadnet <file>`.

### UCI options

| Option | Type | Default | Notes |
|---|---|---|---|
| `Hash` | spin | 256 | TT size in MB (1–65536) |
| `Threads` | spin | 1 | fixed at 1 (single-threaded search) |
| `Move Overhead` | spin | 30 | ms reserved per move for GUI/IO latency |
| `Clear Hash` | button | | |

## Architecture and features

### Board and move generation

- Bitboards with **PEXT sliding attacks** (`_pext_u64`), plus precomputed pawn/knight/king, between and line tables.
- Mailbox board array alongside the bitboards; make/unmake with a state stack. Zobrist keys cover position, pawns+kings
  and the non-pawn pieces per side.
- Pseudo-legal generation (staged: captures and queen promotions, then quiets and under-promotions), followed by a cheap
  legality check using precomputed pinned pieces and checkers. Castling legality is checked at generation time.
- **Perft:** all 126 positions of the supplied suite match at depths 1–6 (756/756). That was 6 minutes into the run.
- Static exchange evaluation (swap algorithm with x-rays).

### Search

Iterative deepening with aspiration windows (δ = 12, widening ×1.5) and a PVS/negamax alpha-beta core. Features:

- **Transposition table:** 32-byte clusters of three 10-byte entries, depth/age replacement, stores the static eval,
  mate-distance adjusted.
- **Quiescence:** captures and queen promotions, SEE and delta pruning, TT probing; all evasions when in check.
- **Pruning:** reverse futility (75·depth, improving-aware), razoring, null move (R = 4 + d/3 + eval term, verified at
  depth ≥ 12), ProbCut, late-move pruning ((5 + d²)/(2 − improving)), futility pruning, history pruning, and SEE pruning
  of quiets and captures. Mate-distance pruning.
- **Reductions/extensions:** log-log LMR table (1.0 + ln d · ln m / 2.0) adjusted by PV/cut-node/improving/history/killer
  status; internal iterative reduction; singular extensions (depth ≥ 6, margin 1·depth) with double and negative
  extensions; check extension.
- **Move ordering:** TT move; captures by MVV plus capture history (bad SEE captures last); two killers; countermove;
  quiets by butterfly history plus 1-, 2- and 4-ply continuation histories (gravity-style updates; bonus 300·d − 150,
  capped at 2400).
- **Pawn-structure correction history:** the static eval is corrected by a learned per-pawn-structure offset
  (+30 Elo when added).
- Repetition detection (2-fold inside the tree, 3-fold before the root), 50-move rule, insufficient-material draws,
  and eval damping as the 50-move counter grows.

### Evaluation (NNUE)

- Architecture: **(768 → 256) × 2 perspectives → SCReLU → 8 output buckets** (by piece count).
- Inputs are mirrored horizontally so each side's own king is always on files a–d.
- Quantised to int16 (QA = 255, QB = 64); AVX2 inference; accumulators updated lazily and incrementally, with a full
  refresh when a king crosses the d/e boundary.
- Trained **from scratch in-session** by a custom multi-threaded C++ trainer (`source/trainer/train.cpp`): Adam,
  cosine learning rate, batch 16384. The target is `sigmoid(eval/400)` fitted to λ·sigmoid(search score/400) +
  (1−λ)·game result.
- **Training data** was all self-play generated in-session: about 7.5M positions from the first handcrafted-eval
  engine, then 86.7M positions from successive NNUE generations. Each game started with 8–9 random plies, used 5,000
  nodes per move, and was adjudicated on large scores or long quiet draws. Positions in check or with a tactical best
  move were skipped.
- The final net (`net13`) was trained for 40 epochs on all 86.7M NNUE-generated positions with λ = 1.0.
- The initial handcrafted evaluation (PeSTO tables + bishop pair + tempo) is still in the code as a fallback,
  but is not used by the final build.

### Time management

- Soft/hard limits, where usable time = T + 0.9·inc·(mtg − 1) − overhead·(mtg + 2), with mtg = 25. Reserving the
  overhead for every remaining move means the engine never spends more than increment minus latency when its clock
  is low.
- The clock starts when `go` is received. The hard limit is capped at a fraction of the remaining time.
- Soft limit scaled by best-move stability, score drops and the share of nodes spent on the best move.
- Stress tests after the fix: 0 time forfeits at 2+0.02 and 1+0.01. Before the fix, 9 of 20 games at 2+0.02 were
  lost on time by 1–6 ms.

### UCI / robustness

- A dedicated search thread, with stdin read on the main thread. `stop`, `isready` and `quit` work during search.
- Always outputs a `bestmove`, including `0000` in mate/stalemate positions.
- The last `info` PV always starts with the reported best move.
- Passes `fastchess --compliance`.

### Deliberately left out

- **Multi-threading, pondering, tablebases, opening book:** not required or not allowed.
- **Larger networks:** a 512-wide hidden layer lowered validation loss but lost about 9–24 Elo in games, because it
  is about 12–20% slower and fits search-score noise.
- **King-bucketed inputs (4 buckets):** also lower loss but −55 and −61 Elo in two separate trials.
- **Non-pawn correction history, several search-parameter variants, PGO and `-funroll-loops`:** measured and rejected.

## How the time was spent

| Elapsed | What happened |
|---|---|
| 0:00–0:20 | Bitboards, PEXT, movegen, perft (full suite passed at 0:06), PeSTO eval, full search, UCI. **v1 in `final/` at 0:16** (≈2950: 81.5% vs stash-21). |
| 0:20–1:00 | Data generator + NNUE inference + trainer written; datagen started on 9–12 processes; correction history (+30). |
| 1:00–2:30 | net1 on 5.3M positions → **+158 vs HCE**; installed (≈3150 vs stash-30). Datagen switched to NNUE engine. |
| 2:30–4:00 | net2 (+179), λ experiments: λ=0.5 −124, **λ=0.9 +144** → nn3b. |
| 4:00–6:00 | λ=1.0 (+28); 512 hidden rejected; dropping HCE data + λ=1.0 → nn5a (+62 in RR). |
| 6:00–9:00 | nn5a calibrated ≈3420 vs stash-37. Search-parameter gauntlets (LMR adopted); NN gains start to saturate. |
| 9:00–11:00 | **Time-management robustness fix** (low-clock forfeits found in stress tests). net7 (+44). |
| 11:00–15:00 | Gauntlets 3–6: LMP, SE depth/margin, aspiration, history bonus adopted (each +10 to +28). net8 (+8). KB4 re-test rejected. |
| 15:00–18:00 | Gauntlets 7–10, TM horizon, fine-tuning experiments; mostly neutral. nn8h calibrated ≈3480. net11 (all 86.7M, 18 epochs) +15. |
| 18:00–20:00 | net12a (26 epochs) +13 → installed and verified. |
| 20:00–22:40 | net13 (40 epochs): +8 over 900 games (LOS 83%), then +4 pooled over 1,400 games. Same code, only the net differs, so installed on expected value → **final build** (frozen 14:52 local, re-verified). README written. |

**Measured vs. judgement:**
- Every network change, every search/time parameter change and the final build were measured in game matches
  (typically 300–1,200 games per decision at 5+0.05 or 10+0.1).
- Accepted on reasoning with only small confirmation: node-fraction time management (+44 in 80 games), the PV-printing
  fix, and insufficient-material draws.

**What went wrong / was cut:**
- The first time-management formula was vulnerable to per-move GUI latency at very low clock times. Stress tests
  caught it at 9 hours; fixed.
- Validation loss turned out to be a poor predictor of playing strength (larger and king-bucketed nets).
- NN gains per retrain shrank from ~+150 to ~+10 once the data came from roughly 3400-level engines.
- The `Hour N` labels in `docs/progress.md` drifted from true elapsed hours after about hour 8. A note in the log
  explains this, and the table below uses true elapsed time.

## Elo by hour

Estimates on the CCRL-blitz scale from `docs/progress.md`. They are anchored on Stash matches where available
(stash-21 ≈ 2710, stash-30 ≈ 3170, stash-33 ≈ 3286, stash-37 ≈ 3424) and interpolated along self-play results in between.

| Hour (elapsed) | Local time | Build in `final/` | Estimated Elo | Evidence |
|---|---|---|---|---|
| 0 | 2026-09-22 16:15 | — | not yet measurable | |
| 1 | 17:15 | v1 (PeSTO) | ≈2950 | vs stash-21: 81.5% (100 g) |
| 2 | 18:15 | nn1 | ≈3150 | vs stash-30: 49% (100 g) |
| 3 | 19:15 | nn2 | ≈3290 | vs stash-33: 50.5% (100 g) |
| 4 | 20:15 | nn3b | ≈3340 | self-play chain |
| 5 | 21:15 | nn3b | ≈3340 | self-play chain |
| 6 | 22:15 | nn4a | ≈3370 | self-play chain |
| 7 | 23:15 | nn5a | ≈3420 | vs stash-37: 49.6% (120 g) |
| 8 | 00:15 | nn5a | ≈3420 | |
| 9 | 01:15 | tm2 | ≈3425 | vs nn5a +5 (300 g) |
| 10 | 02:15 | tm2 | ≈3425 | |
| 11 | 03:15 | nn7 | ≈3440 | vs tm2 +44 (240 g); anchored by later stash-37 runs |
| 12 | 04:15 | nn7s | ≈3450 | vs nn7 +16 (270 g) |
| 13 | 05:15 | nn7s | ≈3450 | |
| 14 | 06:15 | nn8s | ≈3460 | vs stash-37: 55.8% (120 g) |
| 15 | 07:15 | nn8s | ≈3460 | |
| 16 | 08:15 | nn8h | ≈3480 | vs stash-37: 57.75% (200 g) |
| 17 | 09:15 | nn8h | ≈3480 | |
| 18 | 10:15 | nn11 | ≈3495 | vs nn8h +15 (500 g); vs stash-37 63% (100 g) |
| 19 | 11:15 | nn11 | ≈3495 | |
| 20 | 12:15 | nn12a | ≈3500 | vs nn11 +13 (500 g); vs stash-37 60% (100 g) |
| 21 | 13:15 | nn12a | ≈3500 | |
| 22 | 14:15 | nn12a | ≈3500 | |
| 23 | 15:15 | nn13 (final, frozen 14:52) | ≈3505 | vs nn12a +4 (1,400 g) |
| 24 | 16:15 | nn13 (final) | ≈3505 | |

## All assumptions made

- **Identity:** model name "Opus 5.5" → engine `Opus 5.5 chess 24hrs`, author `Opus 5.5`, exe `Opus55chess24hrs.exe`.
- **CPU budget:** "up to physical cores − 2 (= 10) for testing" was read as the fastchess `-concurrency` limit, which
  was always ≤ 10. Self-play data generation and NN training ran on additional hardware threads (the machine has 16
  logical CPUs), so total CPU use was sometimes above 10 cores.
- **Hardware:** development machine i7-1260P (4 performance + 8 efficiency cores). Concurrent test games land on both
  core types; self-play comparisons stay fair, and Stash ladder estimates are somewhat noisy.
- **Unknown fastchess `timemargin`:** assumed it may be 0. Hence Move Overhead 30 ms reserved per remaining move,
  the clock started at `go` receipt, and stress tests at 2+0.02 and 1+0.01.
- **Training data provenance:** networks trained on positions from the engine's own self-play games (random-ply openings)
  count as "values derived in this session". The UHO book was used only for test matches, never for data generation.
- **PeSTO tables:** used from memory as published documentation (chessprogramming.org) for the initial HCE only. No
  internet resources were fetched during the run (see `docs/resources.md`).
- **Hash:** default set to 256 MB to match the rating conditions. Threads fixed at 1.
- **Ponder:** not supported. `go ponder` searches until `stop`, and `ponderhit` stops and returns a move.
- **README timing:** written after the final build was installed and frozen (no further engine changes), as the rules allow.

## Estimated strength

**Best estimate: ≈ 3500 CCRL-blitz-equivalent, uncertainty about ±40 (1σ-ish), more in the absolute sense.**

Evidence (all at 10+0.1, Hash 256, UHO book with colour-reversed pairs, fastchess, this machine):

| Engine | Opponent | Games | Score | Elo diff |
|---|---|---|---|---|
| nn12a (final − ≈4) | stash-37 (≈3424) | 100 | 60.0% (+39 −19 =42) | +70 ± 46 |
| nn11 (final − ≈17) | stash-37 | 100 | 63.0% | +92 ± 49 |
| nn8h (final − ≈32) | stash-37 | 200 | 57.75% | +54 ± 36 |
| nn5a (earlier build) | stash-37 | 120 | 49.6% | −3 ± 49 |

Caveats:
- The Stash ratings are CCRL ratings at longer time controls.
- UHO openings, the 10+0.1 control and a hybrid-core machine all shift results.
- Self-play gains between versions usually overstate gains against other engines.
- The final rating pool also contains engines other than Stash.

A reasonable range for the official result is roughly 3400–3550.

## Official results

Rated by the benchmark operator on 23 September 2026 in 2,020 games, added to the series'
existing rating dataset (7,140 games in total across all six engines).

**Opus 5.5 chess 24hrs: 3463 Elo ±16 (95% CI), CCRL Blitz scale**, from 2,020 games
(+681 =652 −687, 49.9% score). That is first in the series, about 200 Elo above Fable 5.1.

The in-session estimate of ≈3500 was close but slightly high. Measured only against the anchor
engines, Opus 5.5 rates about **3448 ±18**. Its games against the other AI engines imply more
(about 3580, since it scored 86% against both Fable 5.1 and Opus 5), and those games pull the
combined fit up to 3463. The ±16 is statistical only and does not cover that gap, so
"about 3450–3465" is the fairest summary.

### How the rating was measured

- Tool: fastchess 1.8.2, time control **10 s + 0.1 s**, one thread, **64 MB hash**,
  `timemargin 200`. (The development runs used Hash 256.)
- Openings: UHO unbalanced 8-ply book, random order, every opening played with
  both colours.
- Adjudication: resign after 3 moves at ±600 cp (two-sided); draw after move 40
  when 8 consecutive moves stay within ±20 cp; 250-move cap. `-recover` on.
- Format: a 72-game pilot showed Opus 5.5 was stronger than Stash 37, the top of the Stash
  ladder, so eight stronger anchors were added from the live CCRL Blitz list. Opus 5.5 then
  played two gauntlets against nine anchors (1,600 games), plus 420 games against Fable 5.1,
  Opus 5 and Astra 6. Ratings come from an anchored maximum-likelihood Elo fit over all
  7,140 games in the series.
- Hardware: AMD Ryzen AI 9 HX 375 laptop, one game per physical core
  (12 concurrent games).
- Anchors (CCRL Blitz): Stash 37 (3424), Marvin 6.3.0 (3458), Leorik 3.2 (3490),
  Tucano 12.00 (3491), Carp 3.0.1 (3525), Sirius 9.0 (3528), Patricia 5.0 (3539),
  Elixir 3.0 (3567), Motor 0.9.0 (3695). The older engines in the series were rated against
  Stash 20 to Stash 37, Juggernaut and Crafty.
- No time losses, crashes, disconnects or illegal moves in any of Opus 5.5's games.

### Head-to-head

Opponent ratings in parentheses are the other AI engines' fitted values
from this run; the rest are fixed CCRL Blitz anchors. "Implied Elo" is
the rating this single match alone would give.

| Opponent | Rating | W | D | L | Score | Implied Elo |
|---|---:|---:|---:|---:|---:|---:|
| Motor 0.9.0 | 3695 | 3 | 26 | 131 | 10.0% | 3313 |
| Elixir 3.0 | 3567 | 20 | 66 | 74 | 33.1% | 3445 |
| Patricia 5.0 | 3539 | 36 | 59 | 105 | 32.8% | 3414 |
| Sirius 9.0 | 3528 | 49 | 70 | 81 | 42.0% | 3472 |
| Carp 3.0.1 | 3525 | 41 | 62 | 57 | 45.0% | 3490 |
| Tucano 12.00 | 3491 | 46 | 90 | 64 | 45.5% | 3460 |
| Leorik 3.2 | 3490 | 26 | 80 | 54 | 41.2% | 3429 |
| Marvin 6.3.0 | 3458 | 57 | 85 | 58 | 49.8% | 3456 |
| Stash 37 | 3424 | 64 | 56 | 40 | 57.5% | 3477 |
| Fable 5.1 chess 24hrs | (3264) | 127 | 23 | 10 | 86.6% | – |
| Opus 5 chess 24hrs | (3232) | 124 | 26 | 10 | 85.6% | – |
| Astra 6 chess 24hrs | (3143) | 88 | 9 | 3 | 92.5% | – |

Motor looks mis-scaled at these conditions: this match alone implies 3313, far below every
other anchor. Leaving Motor out moves the overall rating to 3471.

### Benchmark series standings

Fitted together over all 7,140 games. Adding Opus 5.5's lopsided wins lowered the older
engines by about 10 Elo compared with their earlier published ratings.

| Engine | Elo | 95% CI | Games | Score |
|---|---:|:---:|---:|---:|
| **Opus 5.5 chess 24hrs** | **3463** | **±16** | **2020** | **49.9%** |
| Fable 5.1 chess 24hrs | 3264 | ±20 | 1420 | 52.7% |
| Opus 5 chess 24hrs | 3232 | ±20 | 1420 | 48.5% |
| Astra 6 chess 24hrs | 3143 | ±20 | 1420 | 46.1% |
| Fable 5 chess 24hrs | 3045 | ±21 | 1200 | 47.4% |
| Sonnet 5 chess 24hrs | 2702 | ±25 | 1200 | 29.5% |

Quote these as "CCRL Blitz scale, 10+0.1 conditions", not as CCRL ratings.

---

### Repository layout

```
final/Opus55chess24hrs.exe         the rated engine
source/src/                        engine source (position, movegen, search, nnue, uci, datagen, bench, perft)
source/trainer/train.cpp           NNUE trainer
source/nets/                       trained networks (net13.nnue = final)
source/snapshots/                  dated snapshots of every installed build (source + exe + net + note)
source/tests/                      test outputs (fastchess logs/PGNs of all matches were kept locally; *.log/*.pgn are git-ignored)
(source/data/ — the 2.9 GB of self-play training data — was deleted after the run; regenerate with `datagen`)
source/build.sh, match.ps1, verify_final.ps1
docs/progress.md                   hourly log;  docs/resources.md  external resources (none fetched)
```
