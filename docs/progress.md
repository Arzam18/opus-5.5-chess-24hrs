# Progress log — Opus 5.5 chess 24hrs

- Start: 2026-09-22T16:15:36-04:00 — Deadline: 2026-09-23T16:15:36-04:00
- Model: Opus 5.5
- Engine name (`id name`): `Opus 5.5 chess 24hrs`
- Author (`id author`): `Opus 5.5`
- Executable: `final/Opus55chess24hrs.exe`
- Toolchain: GCC 15.2.0 (MinGW-w64, scoop), C++17/20, `-O3 -march=x86-64-v3 -flto -static`
- Machine: i7-1260P, 12 physical cores (4P + 8E hybrid), 16 logical. Testing concurrency: **-concurrency 10**.

## Assumptions
- Hybrid CPU: games in concurrent tests land on both P and E cores; self-vs-self comparisons stay fair,
  Stash ladder estimates will be somewhat noisy/pessimistic. Time management is tested on E cores too (a safety margin).
- Unknown timemargin: assume it may be 0 — engine must never exceed its clock; keep an overhead reserve.

## Plan (budget)
- H0–3: bitboard (PEXT) movegen + perft, UCI, alpha-beta PVS + TT + qsearch, PeSTO eval, time management -> first `final/` build.
- H3–8: search features (NMP, LMR, RFP, futility, SEE, history, killers, aspiration...), measured vs Stash + self SPRT-lite.
- H8–20: evaluation strength: either tuned HCE (Texel on self-generated data) or small NNUE trained in-session on self-play data.
- H20–24: freeze, final verification, compliance, sanity match. Last 1.5h: no new features.

## Log
- **FIRST: full perft suite passed** — 2026-09-22 16:21 (all 126 positions, depths 1–6, 756/756; ~190M nps pseudo-legal+legality-filter perft).
- **FIRST: full legal game played** — 2026-09-22 ~16:27 (fastchess v1 vs stash-21, 10+0.1).

### Hour 0 — 2026-09-22 16:15–16:35
- Built from scratch: PEXT bitboards, pseudo-legal movegen + pin/check-aware legality, make/unmake, Zobrist, SEE.
- Search: ID + aspiration, PVS, TT (3-entry clusters), qsearch (SEE/delta pruning), NMP (+verification at depth>=12), RFP, razoring,
  probcut, IIR, LMP, futility, history/SEE pruning, LMR (log table), singular/double/negative extensions, check extension,
  killers, countermove, main/capture/continuation (1,2,4 ply) history. Eval: PeSTO tapered + bishop pair + tempo.
- UCI: stdin thread main loop + search thread; passes fastchess --compliance.
- v1 vs stash-21 @10+0.1, 100 games: +78 -15 =7 (81.5%), Elo +258 ±88 → **est. ~2950 CCRL-blitz-equivalent** (noisy).
- v1 installed in final/. Snapshot source/snapshots/2026-09-22_1633_hour00_v1.
- Started datagen: 9 processes, HCE engine, 5000 nodes/move, 8-9 random opening plies, adjudication → source/data/hce_*.bin (32B/pos).
- Next: NNUE (768->256)x2->8 buckets, SCReLU; trainer written (source/trainer/train.cpp). Train once ~20M+ positions exist.

### Hour 1 — 2026-09-22 17:15
- Added pawn-structure correction history (v2): +30 ±35 vs v1 (200 games @5+0.05, conc 3) → accepted.
- Added node-fraction time management (v3 = v2 + this; not yet separately tested).
- NNUE: inference (AVX2, lazy incremental accumulators, horizontal king mirroring, 8 material output buckets, SCReLU,
  QA=255/QB=64), trainer (C++ threads, Adam, cosine LR, loss = MSE of sigmoid(out) vs 0.7*sigmoid(score/400)+0.3*WDL).
  Verified: incremental == full refresh (debug build over bench), quantized ≈ float eval (±10cp).
- Datagen: 12 procs (incl. hyperthreads). HCE data: 7.5M positions. net1 (256 hidden) trained on 5.3M in 3.5 min.
- nn1 vs v2(HCE) @10+0.1: +158 ±113 after 40 games (71%). Datagen switched to nn1 engine (5000 nodes/move).
- Elo estimate: v1 ≈ 2950 (vs stash21); nn1 likely ≈ 3100+ (not yet measured vs Stash).
- Next: finish nn1 test, install nn1 in final/ after a Stash check; gather NNUE data; net2 on HCE+nn1 data (~20M).

### Hour 2 — 2026-09-22 18:10
- nn1 vs stash-30 @10+0.1 (Hash 256), 100 games: +36 -38 =26 (49%) → **est. ~3150 CCRL-blitz-equivalent**. No time losses/crashes.
- **nn1 installed in final/** (17:35). Snapshot 2026-09-22_1735_hour01_nn1. Verified standalone (KERNEL32 + msvcrt only).
- Node-fraction TM: +44 ±49 (80 games @10+0.1) vs without → kept.
- Time usage check: engine uses ~15.0 of 16.5 s available per game on average (similar to Stash).
- PGO tried: no measurable nps gain → not used. gprof doesn't work on this toolchain → no profiling.
- Datagen switched to nn1 engine at 17:10 (≈8.6M pos/hour, 12 procs). Data: 7.5M HCE + 7.4M nn1.
- Training net2 (256) on all 15M. Next: test net2 vs net1; try λ=0.5 variant; then bigger nets as data grows.

### Hour 3 — 2026-09-22 19:10
- net2 (256, λ=0.7, 7.5M HCE + 7.4M nn1): nn2 vs nn1 +179 ±51 (120 games @10+0.1) → **nn2 installed in final/ (18:36)**.
- nn2 vs stash-33 @10+0.1 Hash 256, 100 games: +35 -34 =31 (50.5%) → **est. ~3290**. No time losses/crashes.
- Datagen switched to nn2 engine (18:36). Data: 7.5M HCE + 11.4M nn1 + ~4M nn2.
- λ experiment: net2b (λ=0.5) vs net2 (λ=0.7): -124 ±89 after 38 games → WDL-heavy target is worse; trying λ=0.9 next.
- Training net3a (λ=0.7) and net3b (λ=0.9) on all ~23M positions in parallel.
- Next: pick best λ; test net3 vs nn2; consider hidden 512 once data ≥ 30M.

### Hour 4 — 2026-09-22 20:00
- Added optional king-bucketed NNUE inputs (compile flag NNUE_KB=4; trainer --kb 4); verified consistent (debug + float/quantized).
- net3a (λ=0.7) / net3b (λ=0.9) on ~23M positions. Round robin @10+0.1 (~120 games each):
  nn3b +144, nn3a -21, nn2 -123 (relative Elo). → higher λ (more weight on search score) is much better with this data.
- **nn3b installed in final/ (19:57)**; datagen switched to nn3b. Data now: 7.5M HCE + 11.4M nn1 + 10.7M nn2.
- Elo estimate: nn2 ≈ 3290 (vs stash-33 50.5%); nn3b ≈ nn2 + ~150–250 (self-play) → maybe ~3400+ (to be verified vs stash-37).
- Training net4a (λ=1.0, 256) and net4b (λ=0.9, hidden 512) on ~29.7M positions.
- Next: RR nn4a / nn4b / nn3b; stash-37 calibration; fix cosmetic PV≠bestmove warning.

### Hour 5 — 2026-09-22 21:15
- Fixed cosmetic "bestmove ≠ PV" warning (print final PV of the chosen move when a partial iteration changed it).
- Non-pawn correction history (per-colour non-pawn keys): -14 ±35 after 120 games → not adopted (kept behind USE_NPCORR).
- net4a (λ=1.0, 256, 29.7M pos) vs nn3b: +20 ±41 after 120 games (running). net4b (λ=0.9, hidden 512): +17 ±85 after 40 (running).
  512 hidden costs only ~12% nps.
- Datagen: nn3b engine, reduced to 8 procs while tests/training run. Data: 7.5M HCE + 11.4M nn1 + 10.7M nn2 + nn3 growing.
- Elo estimate: ~3400 (nn3b, self-play chain; stash-37 check pending).
- Next: finish net tests; train net5 (best λ, maybe 512) on ~40M; calibrate vs stash-37.

### Hour 6 — 2026-09-22 22:20
- nn4a (λ=1.0) vs nn3b: +28 ±26 (300 games) → installed 21:36. nn4b (hidden 512, λ=0.9): -24 ±37 (160 g) → rejected for now.
- net5a (256, λ=1.0, NNUE-generated data only: nn1+nn2+nn3 = 32.4M, HCE data dropped) and net5b (same + 4 king buckets).
  RR @10+0.1 (~122 g each): nn5a +62, nn5b +14, nn4a -78 → **nn5a installed in final/ (22:20)**. KB4 had lower val loss but plays worse (for now).
- Datagen now nn5a engine (10 procs). Data: nn1 11.4M, nn2 10.7M, nn3 10.3M, nn4 4.1M (+ HCE 7.5M unused).
- Elo estimate: nn5a ≈ nn3b + ~140 (self-play) → plausibly ~3450–3500 CCRL-equivalent; stash-37 calibration next.
- Next: stash-37 calibration; next net cycle (~23:45) with nn4/nn5 data; try 512 again with more epochs; search tweaks.
- 22:58 — **nn5a vs stash-37 @10+0.1 Hash 256: 120 games +41 -42 =37 (49.6%) → est. ~3420 CCRL-equivalent.** No time losses.

### Hour 7 — 2026-09-22 23:20
- nn5a calibrated vs stash-37: 49.6% (120 g) → ~3420. nn5a in final/.
- Search params made compile-time tunable (P_LMR_BASE/DIV, P_RFP_*, P_NMP_*, P_QS_FUT). Gauntlet @5+0.05 (180 g each so far):
  t_nmp (NMP without extra staticEval margin) +10, t_lmr (more reduction) +8, t_rfp (60/cp per depth, depth≤10) -29 vs base — inconclusive, running to 400.
- Training net6a (256, 14 epochs) and net6b (512, 16 epochs) on 40.5M NNUE-generated positions (nn1..nn5), λ=1.0.
- Datagen: nn5a engine, 8 procs.
- Next: net6 RR vs nn5a (~00:45); adopt winning search tweaks.

### Hour 8 — 2026-09-23 00:10
- Search gauntlet @5+0.05 (~380 games each vs base): LMR more aggressive (base 1.0, div 2.0) +12 → **adopted**;
  NMP w/o extra margin ±0 → not adopted; RFP 60cp/depth≤10 -22 → rejected.
- Added insufficient-material draw detection (KvK, K+minor vs K) in is_draw.
- net6a (256, 14 ep, 40.5M) done; val loss 0.00246 (net5a 0.00250). net6b (512, 16 ep) still training (val 0.00240 @ep6).
- Testing nn6a (net6a + LMR + insuff. material) vs nn5a @10+0.1 (SPRT 0..10).
- Elo estimate: ~3420 (nn5a vs stash-37 49.6%).

### Hour 9 — 2026-09-23 00:55
- nn6a (net6a + LMR tweak + insuff. material) vs nn5a: +9 ±27 (200 g) — marginal.
- nn6b (hidden 512, 16 epochs, much lower val loss 0.00229 vs 0.00246) vs nn5a: -9 ±39 (118 g) → rejected.
  Conclusion: lower val loss (fitting 5k-node search noise) ≠ Elo; 512's ~20% nps cost not repaid. Stay with 256.
- Plan: keep generating data (nn5a, 10 procs, ~7M/h), retrain 256 around 05:00 on ~70M; meanwhile search-parameter gauntlets.
- Gauntlet 2 running @5+0.05 (base = net5a + adopted LMR): rfp90, lmr2 (1.25/1.9), futlo (70+70·d), qs120.
- Elo estimate: ~3420 (unchanged; final/ = nn5a).
- 01:05 — **Time-management robustness fix.** Stress test at 2+0.02 showed 9/20 time forfeits (1–6 ms overruns) for the old TM
  (spent ~0.75·inc per move, but GUI-measured latency pushed usage above the increment → clock drained to zero).
  New: usable time = T + 0.9·inc·(mtg−1) − overhead·(mtg+2), clock started at `go` receipt. Result: 0/30 forfeits at 2+0.02,
  0/20 at 1+0.01. At 10+0.1 the allocation is essentially unchanged (≈454 ms on move 1).
  **Installed tm2 (net5a + LMR tweak + insufficient-material draws + TM fix) in final/.**

### Hour 10 — 2026-09-23 01:40
- Gauntlet 2 (@5+0.05, 200 g each): rfp90 -10, lmr2 -16, futlo -14, qs120 +2 relative; base best → current params kept.
- Default Hash raised to 256 MB (matches rating conditions). Wrote source/verify_final.ps1 (id, DLLs, perft, compliance) — final/ passes.
- tm2 (installed) vs nn5a @10+0.1 Hash 256: +14 ±37 after 122 g (running to 300) — no time losses.
- Training net7 (256, λ=1.0, 14 ep) on 54.5M NNUE positions (nn1–nn5). Datagen 7 procs (nn5a engine).
- Prepared gauntlet 3: aspiration 12, SE depth 6, LMP base 5, NMP R base 3.
- Elo estimate ≈ 3420–3440.

### Hour 11 — 2026-09-23 02:30
- tm2 vs nn5a final: +4.6 ±22.7 (300 g, 10+0.1, all normal terminations) → tm2 stays in final/ (robustness gain, strength ≈ equal).
- net7 (256, λ=1.0, 14 ep, 54.5M) done. nn7 vs tm2 SPRT running: +22 ±44 after 80 g.
- Gauntlet 3 (@5+0.05, 140 g each): lmp5 +37 vs base, se6 +22, asp12 +7, nmpr3 -7 (relative to base; running).
- Elo estimate ≈ 3430.

### Hour 12 — 2026-09-23 03:10
- **nn7 (net7) vs tm2: +44 ±25 (240 g @10+0.1) → nn7 installed in final/ (02:45).** Datagen switched to nn7 (10 procs). nn5 data final: 23.3M.
- Gauntlet 3 final (@5+0.05, 280 g each vs base): lmp5 +24, asp12 +17, se6 +10, nmpr3 -17.
- Combined candidates on net7: c1 = lmp5+se6, c2 = lmp5+se6+asp12 → gauntlet vs nn7 @10+0.1 running.
- Elo estimate: nn5a ≈ 3420 (stash-37 49.6%) + tm2 ≈ +5 + net7 ≈ +44 → **~3460**.
- Plan: adopt best combo; train net8 on ~75M (nn1–nn7) around 04:30; stash-37 recalibration; start freezing ~13:30.
- 03:57 — Gauntlet 4 @10+0.1 (~270 g each vs nn7): c1 (lmp5+se6) +17, c2 (lmp5+se6+asp12) +16 → c2 params adopted as defaults
  (LMP base 5, SE depth 6, aspiration 12). **nn7s (= c2) installed in final/.** Training net8 on 64.9M. Gauntlet 5 started
  (probcut 150, SE margin 1·d, LMR hist div 6000, LMP base 7).

### Hour 13 — 2026-09-23 04:05
- final/ = nn7s (net7 + tuned search). Estimated ≈ 3470 (chain: stash-37 49.6% for nn5a, then +5, +44, +16 self-play).
- Running: net8 training (ETA ~04:40), gauntlet 5, datagen (nn7 engine, 6 procs).
- Plan: net8 test ~04:45; stash-37 recalibration of the final candidate ~06:00; code freeze ~13:30, final verification 14:00–15:30.

### Hour 14 — 2026-09-23 04:50
- net8 (256, 14 ep, 64.9M positions nn1–nn5 + 5.1M nn7) — nn8 vs nn7s: +14 ±29 after 200 g @10+0.1 (running).
- Gauntlet 5 (@5+0.05, 220 g each): SE margin 1·depth +16; LMP 7 −2, hist-div 6000 −5, probcut 150 −9 → only sem1 candidate.
- Re-testing king buckets: net9kb (KB=4) on the same 64.9M (ETA ~05:50).
- Datagen reduced to 4 procs (nn7 engine) to make room for tests/training; data total ≈ 67M NN positions.
- Elo estimate ≈ 3470–3480.

### Hour 15 — 2026-09-23 05:20
- nn8 vs nn7s final: +8.7 ±21 (400 g @10+0.1) — marginal. Gauntlet 5 final: SE margin 1·depth +15 (280 g) → adopted as default.
- Candidate nn8s (net8 + SE margin 1) vs installed nn7s @10+0.1, 500 g: running (+4 after 80).
- net9kb (KB=4) training: val loss 0.00242 @ep10 (net8 no-KB: 0.00250 @ep14) — to be tested in play (~05:45).
- Gauntlet 6 prepared (history bonus ±, razoring margins ±).
- Datagen 7 procs (nn7). Elo estimate ≈ 3470–3480.

### Hour 16 — 2026-09-23 06:10
- **nn8s (net8 + SE margin 1·depth) vs nn7s: +8.3 ±19.4 (500 g @10+0.1, all normal) → installed in final/ (06:05).**
- King buckets re-tested with 2× data: nn9kb vs nn8s −61 after 40 g → rejected for good (lower val loss again did not translate to Elo).
- Training net10 (256, 16 ep) on all 75.4M NNUE positions (ETA ~07:30). Gauntlet 6 running (history bonus ±20%, razoring margins ±).
- Elo estimate ≈ 3480 (self-play chain from the stash-37 anchor; recalibration vs stash-37 planned after gauntlet 6).
- Plan to deadline: net10 test ~07:45; stash-37 calibration; final candidate chosen by ~12:30; freeze 13:30; verification 13:30–15:30.

### Hour 17 — 2026-09-23 07:00
- Gauntlet 6 (@5+0.05, 220 g each): history bonus ×1.2 (hb300: 300·d−150, cap 2400) +14; ×0.8 −14 (monotone) → **hb300 adopted**;
  razoring margins ±: neutral → unchanged.
- nn8s (final/) vs stash-37 @10+0.1 Hash 256: 53.8% after 40 g (running to 120).
- net10 (16 ep, 75.4M) nearly done; will test nn10 (net10 + hb300) and nn8h (net8 + hb300) vs nn8s.
- Elo estimate ≈ 3480.
- 07:07 — **nn8s vs stash-37 @10+0.1 Hash 256: 120 g, +47 −33 =40 (55.8%, +41 ±44) → est. ~3460 CCRL-equivalent.**
- 07:38 — Gauntlet 7 @10+0.1 (212 g each vs nn8s): nn8h (net8 + hb300) 54.0% (+28 ±28), nn10 (net10 + hb300) 51.7% (+12).
  **nn8h installed in final/.** net10 (75M, 16 ep) not better than net8 → NN gains saturated at this data quality; focus shifts to search params.
  Gauntlet 8 started (@5+0.05): check-ext only if SEE≥0, double-ext margin 15/40, capture-LMR 0.

### Hour 18 — 2026-09-23 07:50
- final/ = nn8h (net8 + tuned search incl. hb300). Estimated ≈ 3480–3490 (nn8s ≈ 3460 vs stash-37, nn8h +28 over nn8s).
- Gauntlet 8 running; gauntlet 9 prepared (hb350, history-pruning threshold 2000/4500, hist-bonus offset 50).
- Datagen reduced to 4 procs (NN gains saturated); CPU prioritised for search-parameter gauntlets.
- Remaining plan: gauntlets 8–9 (+ maybe 10) until ~12:00 → combine winners → 10+0.1 confirmation vs nn8h →
  freeze by 13:30 → final verification (verify_final.ps1, stash-37 sanity match) → done by ~15:30.
- 07:52 — Gauntlet 8 (@5+0.05, ~80 g each) stopped early: base beat all (check-ext SEE≥0 −48, capture-LMR0 −44, double-ext 40 −27, 15 −24) → none adopted.
  Gauntlet 9 started (hb350, history-pruning 2000/4500, hist-bonus offset 50).
- 08:07 — Gauntlet 9 (@5+0.05, 100 g each): base best; hb350 −3, hist-offset 50 ±0, history-pruning 2000 −17 / 4500 −31 → current values
  look like a local optimum; nothing adopted. net8ft = net8 fine-tuned 4 ep @lr 2e-4 on newest 47M (nn5+nn7); testing vs nn8h @10+0.1.
- 08:28 — nn8ft vs nn8h: +5 ±24 (278 g) → no gain, net8 kept. TM gauntlet @10+0.1 started (moves-to-go horizon 20 / 30 vs 25).

### Hour 19 — 2026-09-23 08:30
- final/ = nn8h. Search params appear locally optimal (gauntlets 8–9 found nothing better); NN retrains/fine-tunes saturated.
- Remaining experiments: time-management horizon; then freeze. Estimated Elo ≈ 3480–3490.
- 08:55 — TM gauntlet @10+0.1 (~118 g each): mtg20 +4, mtg30 +1 vs base (noise) → unchanged (25). Datagen stopped (86.7M NN positions total).
  Training net11 (256, 18 ep, all 86.7M, 6 threads). Gauntlet 10 started (quiet-SEE 15/40, capture-SEE 60/120).
- 09:12 — Gauntlet 10 (@5+0.05, 160 g each): capture-SEE 60 +5, 120 −6, quiet-SEE 15 −12, 40 −14 vs base → nothing adopted.
  Started 200-game calibration nn8h vs stash-37 (@10+0.1, Hash 256).

### Hour 20 — 2026-09-23 09:15
- final/ = nn8h. Search appears locally optimal for single-parameter changes (gauntlets 8–10 found nothing).
- Running: net11 (all 86.7M, 18 ep; ETA ~09:50) and stash-37 calibration of nn8h.
- Next: test nn11 vs nn8h; then freeze (target 13:30 at the latest) and final verification.

> **Note on labels:** from about "Hour 8" onward the entries were written more often than hourly, so the "Hour N" labels
> are check-in numbers, not elapsed hours. Elapsed time = timestamp − 2026-09-22 16:15 (e.g. the "Hour 20" entry at 09:15 is 17 h elapsed).
> Subsequent entries are labelled with true elapsed hours.
- 09:27 — **nn8h (final/) vs stash-37 @10+0.1 Hash 256: 200 g, +83 −52 =65 (57.75%, +54 ±36) → est. ~3480 CCRL-equivalent.** All 200 normal terminations.
- Assumption (CPU use): the rule "up to physical cores − 2 (=10) for testing" was interpreted as the fastchess `-concurrency`
  limit, which was always ≤ 10. Self-play data generation and NN training (not game testing) ran concurrently on the
  remaining/extra hardware threads (16 logical CPUs), at times making total CPU use exceed 10 cores.
- 09:41 — net11 (all 86.7M, 18 ep) done; testing nn11 vs nn8h @10+0.1 (500 g, conc 10).
- 10:10 — **nn11 (net11: all 86.7M, 18 epochs) vs nn8h: +14.6 ±17.8 (500 g @10+0.1, all normal) → nn11 installed in final/.**
  Estimated ≈ 3480 + 15 ≈ **3495**.

### Elapsed hour 18 — 2026-09-23 10:15
- final/ = nn11; verify_final.ps1 passes (id, standalone DLLs, perft 630/630, fastchess compliance, Hash 256).
- Last NN round: net12a (from scratch, 26 epochs) and net12b (warm restart from net11, 8 ep, lr 3e-4) on all 86.7M.
- Elo estimate ≈ 3495 (stash-37 anchor 3424; nn8h +54 vs stash-37 in 200 g; nn11 +15 vs nn8h).
- Plan: test net12b ~10:50, net12a ~12:05; freeze by 13:30; final verification + stash sanity match; finish ≥30 min before 16:15.
- 10:58 — nn12b (net11 warm-restart, 8 ep lr 3e-4) vs nn11: −6.5 ±29 (160 g) → rejected. Waiting for net12a (26 epochs from scratch, ETA ~11:40).

### Elapsed hour 19 — 2026-09-23 11:15
- final/ = nn11. Sanity match nn11 vs stash-37 running (57.6% after 46 g).
- net12a (26 epochs) ETA ~11:40 → last candidate; 500-game test vs nn11 → decide by ~12:15.
- Then: FREEZE. Final verification (verify_final.ps1), snapshot, final progress entry. No new features after 12:30.
- 11:11 — **nn11 (final/) vs stash-37 @10+0.1 Hash 256: 100 g, +44 −18 =38 (63.0%, +92 ±49).** All normal terminations.
  Combined with nn8h's 200 g (+54) and nn11 ≈ nn8h +15 → **estimate ≈ 3490–3510 CCRL-blitz-equivalent** (±~40).

### Elapsed hour 20 — 2026-09-23 12:05
- **nn12a (net12a: 26 epochs) vs nn11: +13.2 ±19.4 (500 g @10+0.1, all normal) → FINAL candidate installed in final/ (12:01).**
  verify_final.ps1 passes; runs from a minimal PATH (no MinGW). Snapshot source/snapshots/2026-09-23_1201_FINAL_nn12a.
- nn12b (warm restart) rejected (−6.5, 160 g).
- Last experiment (time permitting): net13 = 40 epochs on all data (trend: 16→18→26 epochs each helped). Stash-37 sanity match of final/ running.
- Hard stop for any change to final/: 14:45. Estimated Elo ≈ 3500–3510.
- 12:20 — **final/ (nn12a) vs stash-37 @10+0.1 Hash 256: 100 g, +39 −19 =42 (60.0%, +70 ±46).** All normal terminations.
  Pooled stash-37 evidence for the last builds (nn8h 200 g 57.8%, nn11 100 g 63%, nn12a 100 g 60%) → final ≈ **3500 ±40** CCRL-blitz-equivalent.

### Elapsed hour 21 — 2026-09-23 13:15 (written 12:52)
- final/ = nn12a (verified). net13 (40 epochs) training, ETA ~13:30; 500-game test vs nn12a to follow (~14:00).
- Rule for the last change: install nn13 only if clearly ≥ nn12a (LOS ≳ 90%) with all-normal terminations, then re-run verify_final.ps1.
- 13:58 — nn13 (net13: 40 epochs, val 0.002521) vs nn12a: +8.3 ±17.4 (500 g, all normal), LOS ≈ 83% < 90% bar → running 400 more games before deciding (decision by 14:30).
- 14:21 — nn13 extra 400 g: +7.0 ±19.4. Pooled 900 g: +225 −205 =470 (51.1%, ≈ +8 Elo, LOS ≈ 83%) → below the pre-set 90% bar → **not installed.**

### Elapsed hour 22 — 2026-09-23 14:25 — FINAL BUILD FROZEN
- **final/Opus55chess24hrs.exe = nn12a** (md5 14be680dad97a3d87a532ecef39a2740), identical to
  source/snapshots/2026-09-23_1201_FINAL_nn12a/Opus55chess24hrs.exe. Build: `EXTRA_FLAGS="-DEMBED_NET=nets/net12a.nnue" bash build.sh`.
- Verified: id name "Opus 5.5 chess 24hrs", author "Opus 5.5"; standalone (KERNEL32 + msvcrt only; runs with PATH=System32 only);
  perft 630/630; fastchess --compliance passes; Hash default 256; stress tests 2+0.02 / 1+0.01 with 0 time forfeits.
- Final strength evidence @10+0.1 Hash 256 vs stash-37 (CCRL ~3424): final build 100 g 60.0%; predecessors nn11 100 g 63.0%,
  nn8h 200 g 57.75%. **Best estimate ≈ 3500 ±40 CCRL-blitz-equivalent** (single-machine, hybrid CPU, UHO book; ladder estimate only).
- No further engine changes. Remaining time: write README.md (allowed once final build installed and no further changes planned).
- 14:23 — README.md written. Re-considered nn13: code identical to final (only the embedded net differs), 900 games show
  +7.7 Elo (LOS 83%). Choosing by expected Elo rather than the conservative 90% bar is reasonable when reliability risk is nil,
  so running 500 more games @10+0.1 (Hash 256); install nn13 only if pooled 1400-game result stays positive. Hard cutoff 15:15.
- 14:51 — nn13 third run (Hash 256): 500 g +116 −119 =265 (−2). **Pooled 1400 g: +341 −324 =735 (50.6%, ≈ +4 Elo)** → positive as required.

### Elapsed hour 22.6 — 2026-09-23 14:53 — FINAL BUILD (final, frozen)
- **final/Opus55chess24hrs.exe = nn13** (md5 8373edc32035a6c0d6309927a684841f) = source/snapshots/2026-09-23_1452_FINAL_nn13/.
  Same code as nn12a; embedded net13 (256 hidden, λ=1.0, 40 epochs on all 86.7M self-play positions).
  Build: `EXTRA_FLAGS="-DEMBED_NET=nets/net13.nnue" bash build.sh`.
- Re-verified: id/author, standalone DLLs, minimal-PATH run, perft 630/630, fastchess compliance, Hash 256.
- Final estimate ≈ 3500 ±40 CCRL-blitz-equivalent. **No further changes.** README.md updated accordingly.
