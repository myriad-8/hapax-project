#!/usr/bin/env python3
"""Plots from results/results.csv -> results/plots/*.png  (needs pandas, matplotlib)"""
import os, sys
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

src = sys.argv[1] if len(sys.argv) > 1 else "results/results.csv"
df = pd.read_csv(src)
bad = df[df.ok != 1]
if len(bad):
    print(f"WARNING: {len(bad)} runs failed the correctness check and are excluded")
df = df[df.ok == 1]
os.makedirs("results/plots", exist_ok=True)

g = df.groupby(["lock", "threads", "cs", "ncs", "burst_ms"]).agg(
    mops_m=("mops", "mean"), mops_s=("mops", "std"),
    p99_m=("p99_ns", "mean"), p99_s=("p99_ns", "std"),
    jain_m=("jain", "mean"), jain_s=("jain", "std")).reset_index()

metrics = [("mops", "Throughput (M ops/s)", False),
           ("p99", "P99 lock wait (ns)", True),
           ("jain", "Fairness (Jain index, 1 = perfect)", False)]

for (cs, ncs, bm), sub in g.groupby(["cs", "ncs", "burst_ms"]):
    for key, label, logy in metrics:
        fig, ax = plt.subplots(figsize=(6.4, 4.2))
        for lock, s in sub.groupby("lock"):
            s = s.sort_values("threads")
            ax.errorbar(s.threads, s[key + "_m"], yerr=s[key + "_s"].fillna(0),
                        marker="o", capsize=3, label=lock)
        ax.set_xscale("log", base=2)
        if logy: ax.set_yscale("log")
        ax.set_xlabel("threads"); ax.set_ylabel(label)
        ax.set_title(f"CS={cs} iters, non-CS={ncs} iters" + (f", burst={bm}ms" if bm else ""))
        ax.grid(True, alpha=.3); ax.legend(fontsize=8)
        fig.tight_layout()
        fig.savefig(f"results/plots/{key}_cs{cs}_ncs{ncs}_b{bm}.png", dpi=150)
        plt.close(fig)
print("plots written to results/plots/")
