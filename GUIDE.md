# Adaptive Hapax Locks: Complete Plan (v2)

Goal: faithful **Hapax (IW)** and **HapaxVW** baselines (ported from the paper's Listings 2 and 3),
plus your **adaptive lock** built on top, benchmarked head to head.
All locks pass correctness and ThreadSanitizer tests. I could only run on a 1-core sandbox, so there are
**no real performance numbers yet** and you must produce them on a multicore machine.

## What is in the folder
| File | What it is |
|---|---|
| `src/hapax_common.h` | hapax allocator (64K blocks, 48+16 split), 4096-slot waiting array, `ToSlot` hash |
| `src/lock_hapax_iw.h` | **Hapax with invisible waiters**, paper Listing 2 |
| `src/lock_hapax_vw.h` | **HapaxVW**, paper Listing 3 |
| `src/lock_hapax_ad.h` | **Your adaptive lock** (`hapax_ad`; `hapax_adv` adds bounded visible waiting) |
| `src/lock_tas/ticket/pthread.h` | baselines |
| `src/bench.c` | MutexBench-style harness: throughput, P50/P99 wait, fairness (min/max ops, Jain), burst mode, built-in exclusion check |
| `tests/test_mutex.c` | exclusion, multi-lock, random release order |
| `scripts/` | `run_all.sh`, `plot.py`, `smoke.sh` |

## Things in the port you MUST verify against the paper PDF
1. **`ToSlot` hash** (`hapax_common.h`): the hash line is garbled in the copy I could read. The prose says "take the high-order bits (zone), multiply by 17, mask to 4096". I implemented exactly that.
2. **HapaxVW salt**: I used the low 32 bits of the lock address, as the listing's `salt` suggests. Check how it is combined.
3. **Memory orders**: where the listing writes no order I used the C++ default (seq_cst).
4. **Lock context**: the paper stores the hapax in the lock body (`owner`), as I do.
Write a "paper vs my port" table in the report. Reviewers will check this.

## Your adaptive lock: what it actually does
`hapax_ad` = HapaxVW + a **history-gated uncontended unlock fast path**:
- Each thread keeps a private moving average (no shared writes) of "did my acquisitions have to wait?".
- If this acquisition did not wait AND the average is low, unlock first tries `CAS(Arrive: my hapax -> pred)`. If nobody arrived, the lock is back to unlocked and all waiting-array traffic is skipped. If the CAS fails, a waiter exists and the normal VW unlock runs.
- `hapax_adv` additionally makes waiters vacate their slot after K pauses and fall back to spinning on Depart (`-DAD_VACATE=K`).

**Design insight to put in your paper: you cannot mix IW and VW waiters on one array.** A VW waiter treats *any* change of its slot as handover, so an IW unlocker storing into a colliding slot would give a VW waiter the lock while it is still held. A true hybrid needs two arrays. That's a legitimate research direction (`hapax_hybrid`).

## Reality check on "publish a paper"
Be honest with yourself here, it will save you months:
- The authors already *mention* the rollback optimization, the "if I waited, bet on contention" heuristic, and visible-then-vacate waiting as optional ideas, but did not evaluate them. So those alone are **not novel**. Your possible contributions: (a) a rigorous evaluation of these adaptive policies, (b) a policy or mechanism that goes beyond what they sketched (e.g. the two-array hybrid, collision-triggered zone shifting), (c) a negative or trade-off result, clearly explained.
- Their evaluation used a 128-core ARM and a 512-thread AMD machine, LevelDB, and LD_PRELOAD interposition, median of 11 runs. A laptop result will not convince PPoPP-level reviewers. Realistic targets: course report -> arXiv/tech report -> student or workshop track. Ask your faculty about access to a many-core server.
- Before claiming novelty, search for papers citing Hapax Locks and check what exists.

## Phases
**0. Setup (30 min).** Linux, `build-essential`, `python3-pandas`, `python3-matplotlib`. Record `nproc` and `lscpu`. `git init`, commit.
**1. Verify (30 min).** `make all && make check && make tsan && make check-tsan && scripts/smoke.sh`. All PASS.
**2. Paper check (1-2 days).** The 4 items above. Read the paper's Section 4 (visible waiters) carefully again. Add the FIFO test and the "remove release ordering -> TSan reports a race" demo.
**3. Add more baselines (2-3 days).** The paper compares MCS, CLH, TWA, HemLock, Reciprocating, Tidex, Ticket. Add at least **MCS and Reciprocating** (arXiv 2501.02380) plus TWA if you can, as new `src/lock_*.h` files with the same `lock_init/lock_lock/lock_unlock` interface, and register them in the Makefile.
**4. Experiments (several days, mostly waiting).** Mirror the paper's MutexBench:
  - max contention: `-c 1 -n 0`; moderate: `-c 1 -n 500`
  - run **10 s** (`DUR=10`), **11 repetitions**, report the median
  - threads: 1,2,4,... up to all hardware threads
  - keep randomized non-critical sections (`-R 1`, default); the paper warns fixed ones create misleading persistent schedules
  - also bursty: `BURST_MS=50`
  - `LOCKS="tas ticket pthread hapax_iw hapax_vw hapax_ad hapax_adv" DUR=10 REPS=11 CS_LIST="1" NCS_LIST="0 500" ./scripts/run_all.sh`
  - then `python3 scripts/plot.py`
  - use `perf stat -e cache-misses,context-switches` on key configs, and compare unpinned (paper default) with pinned (`-p 0` vs `-p 1`)
**5. Analyse.** Where does hapax_ad beat or lose to hapax_vw, and why? Expect: rollback saves work at low contention and bursty phases, costs a little at sustained contention. Report variance, and report losses honestly.
**6. Tune.** `AD_EMA_MAX` (0-1024) and `AD_VACATE` K. Sweep them, don't pick by feel.
**7. Stretch research.** The two-array hybrid, collision-triggered zone shifting, a LevelDB benchmark via an LD_PRELOAD shim.
**8. Write-up.** Intro, background, algorithm (with a 3-thread trace for IW and VW), your design + correctness argument (why rollback and vacate are safe), methodology, results, limitations (4096-slot collisions, 64-bit overflow, spinning cost, hardware), related work, conclusion.
**9. Decide on publication** with your faculty once real many-core data exists.

## Correctness argument you must be able to give (viva and paper)
- **Rollback is safe**: it succeeds only if Arrive still equals my hapax, meaning no thread arrived, so nobody waits and restoring Arrive := pred (== Depart at my acquisition) leaves a consistent unlocked state.
- **Vacate is safe**: after a successful vacate CAS, the unlocker's positive-handover CAS fails, so it stores Depart, which the waiter spins on. A failed vacate CAS means the slot already changed, i.e. handover happened.
