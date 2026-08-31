"""Create matched, publication-style TVLA figures capped at 5,000 traces."""

from __future__ import annotations

import csv
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parent.parent
MASKED = ROOT / "tvla_results" / "masked-mul_20260810_161841"
UNMASKED = ROOT / "tvla_results" / "unmasked-mul_20260810_135104"
OUTPUT = ROOT / "tvla_results" / "comparison_5k"
CAP = 5000
THRESHOLD = 4.5
SAMPLE_RATE_HZ = 5_000_000.0
TRIGGER_INDEX = 2010
ROI_START = 2010
ROI_STOP = 8650
IEEE_COLUMN_WIDTH_IN = 3.5
IEEE_COMPARISON_HEIGHT_IN = 2.7
IEEE_CAPTION_FONT_SIZE_PT = 8.0


def load_capture(path: Path):
    traces = np.load(path / "traces_int8.npy", mmap_mode="r")
    records = []
    with (path / "capture_log.csv").open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            index = int(row["trace_index"])
            if (row["status"].startswith("ok") and
                    int(row["samples"]) == traces.shape[1] and
                    0 <= index < traces.shape[0]):
                records.append((index, int(row["class"])))
            if len(records) == CAP:
                break
    values = np.asarray(records, dtype=np.int64)
    if len(values) != CAP:
        raise RuntimeError(f"{path.name}: found only {len(values)} valid traces")
    return traces, values[:, 0], values[:, 1]


def analyze(path: Path):
    traces, rows, labels = load_capture(path)
    width = ROI_STOP - ROI_START
    sums = np.zeros((2, width), dtype=np.float64)
    sums2 = np.zeros((2, width), dtype=np.float64)
    counts = np.zeros(2, dtype=np.int64)
    for cls in (0, 1):
        selected = rows[labels == cls]
        counts[cls] = len(selected)
        for start in range(0, len(selected), 256):
            block = np.asarray(
                traces[selected[start:start + 256], ROI_START:ROI_STOP],
                dtype=np.float64,
            )
            sums[cls] += block.sum(axis=0)
            sums2[cls] += np.square(block).sum(axis=0)
    means = sums / counts[:, None]
    variances = (sums2 - sums * sums / counts[:, None]) / (counts[:, None] - 1)
    denominator = np.sqrt(variances[0] / counts[0] + variances[1] / counts[1])
    t_value = np.divide(means[0] - means[1], denominator,
                        out=np.zeros(width), where=denominator > 0)
    sample_index = np.arange(ROI_START, ROI_STOP)
    time_ms = (sample_index - TRIGGER_INDEX) / SAMPLE_RATE_HZ * 1e3
    return {"t": t_value, "means": means, "counts": counts,
            "time_ms": time_ms, "sample_index": sample_index}


def style(axis):
    axis.grid(alpha=0.22, linewidth=0.6)
    axis.spines[["top", "right"]].set_visible(False)


def save_single_tvla(name: str, result, color: str):
    fig, ax = plt.subplots(figsize=(10, 4.2))
    ax.plot(result["time_ms"], result["t"], color=color, linewidth=0.75)
    ax.axhline(THRESHOLD, color="#c62828", linestyle="--", linewidth=1)
    ax.axhline(-THRESHOLD, color="#c62828", linestyle="--", linewidth=1)
    ax.axvline(0, color="0.4", linestyle=":", linewidth=0.8)
    ax.set(xlabel="Time from trigger (ms)", ylabel="Welch t-statistic",
           title=f"{name.capitalize()} multiplication: first-order TVLA (5,000 traces)")
    style(ax)
    fig.tight_layout()
    fig.savefig(OUTPUT / f"{name}_tvla_5k.png", dpi=300)
    fig.savefig(OUTPUT / f"{name}_tvla_5k.pdf")
    plt.close(fig)


def save_ieee_column_tvla(masked, unmasked):
    """Save a combined TVLA figure at its final IEEE single-column size."""
    rc = {
        "font.family": "DejaVu Sans",
        "font.sans-serif": ["DejaVu Sans"],
        "font.size": IEEE_CAPTION_FONT_SIZE_PT,
        "axes.labelsize": IEEE_CAPTION_FONT_SIZE_PT,
        "axes.titlesize": IEEE_CAPTION_FONT_SIZE_PT,
        "xtick.labelsize": IEEE_CAPTION_FONT_SIZE_PT,
        "ytick.labelsize": IEEE_CAPTION_FONT_SIZE_PT,
        "axes.linewidth": 0.65,
        "lines.solid_capstyle": "round",
        "path.simplify": False,
        "pdf.fonttype": 42,
        "pdf.compression": 9,
        "ps.fonttype": 42,
        "savefig.dpi": 1200,
    }
    with plt.rc_context(rc):
        fig, axes = plt.subplots(
            2,
            1,
            figsize=(IEEE_COLUMN_WIDTH_IN, IEEE_COMPARISON_HEIGHT_IN),
            sharex=True,
            sharey=True,
            constrained_layout=True,
        )
        panels = (
            (axes[0], "(a) Unmasked", unmasked, "#D55E00"),
            (axes[1], "(b) Masked", masked, "#0072B2"),
        )
        for ax, title, result, color in panels:
            ax.plot(
                result["time_ms"],
                result["t"],
                color=color,
                linewidth=0.55,
                rasterized=False,
            )
            ax.axhline(0, color="0.35", linewidth=0.45, zorder=0)
            ax.axhline(THRESHOLD, color="#B00020", linestyle="--", linewidth=0.85)
            ax.axhline(-THRESHOLD, color="#B00020", linestyle="--", linewidth=0.85)
            ax.text(
                0.02,
                0.94,
                title,
                transform=ax.transAxes,
                ha="left",
                va="top",
                fontsize=IEEE_CAPTION_FONT_SIZE_PT,
                fontweight="normal",
            )
            ax.set_ylim(-6.8, 6.8)
            ax.set_yticks((-6, -3, 0, 3, 6))
            ax.tick_params(direction="out", width=0.65, length=2.6, pad=2)
            ax.grid(axis="x", alpha=0.18, linewidth=0.45)
            ax.spines[["top", "right"]].set_visible(False)

        axes[0].text(
            0.985,
            0.845,
            r"threshold: $|t|=4.5$",
            transform=axes[0].transAxes,
            ha="right",
            va="bottom",
            fontsize=IEEE_CAPTION_FONT_SIZE_PT,
            color="#8A0018",
        )
        axes[-1].set_xlabel("Time from trigger (ms)", labelpad=2)
        fig.supylabel(r"Welch $t$-statistic")
        fig.set_constrained_layout_pads(
            w_pad=0.01,
            h_pad=0.01,
            wspace=0.0,
            hspace=0.015,
        )

        stem = OUTPUT / "masked_vs_unmasked_tvla_5k_ieee_column"
        fig.savefig(stem.with_suffix(".pdf"), dpi=1200)
        fig.savefig(stem.with_suffix(".png"), dpi=600)
        plt.close(fig)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    masked = analyze(MASKED)
    unmasked = analyze(UNMASKED)
    save_single_tvla("masked", masked, "#1565c0")
    save_single_tvla("unmasked", unmasked, "#ef6c00")
    save_ieee_column_tvla(masked, unmasked)

    fig, axes = plt.subplots(2, 1, figsize=(10, 7.2), sharex=True)
    for ax, title, result, color in (
        (axes[0], "Unmasked", unmasked, "#ef6c00"),
        (axes[1], "Masked", masked, "#1565c0"),
    ):
        ax.plot(result["time_ms"], result["t"], color=color, linewidth=0.75)
        ax.axhline(THRESHOLD, color="#c62828", linestyle="--", linewidth=1)
        ax.axhline(-THRESHOLD, color="#c62828", linestyle="--", linewidth=1)
        ax.set_ylabel("Welch t-statistic")
        ax.set_title(f"{title} ({result['counts'][0]} fixed, {result['counts'][1]} random)")
        style(ax)
    axes[-1].set_xlabel("Time from trigger (ms)")
    fig.tight_layout()
    fig.savefig(OUTPUT / "masked_vs_unmasked_tvla_5k.png", dpi=300)
    fig.savefig(OUTPUT / "masked_vs_unmasked_tvla_5k.pdf")
    plt.close(fig)

    fig, axes = plt.subplots(2, 1, figsize=(10, 7.2), sharex=True)
    for ax, title, result in ((axes[0], "Unmasked", unmasked),
                              (axes[1], "Masked", masked)):
        ax.plot(result["time_ms"], result["means"][0], label="Fixed class",
                color="#6a1b9a", linewidth=0.8)
        ax.plot(result["time_ms"], result["means"][1], label="Random class",
                color="#00897b", linewidth=0.8, alpha=0.9)
        peak = int(np.argmax(np.abs(result["t"])))
        ax.axvline(result["time_ms"][peak], color="#c62828", linestyle="--",
                   linewidth=0.9, label="Maximum |t|")
        ax.set_ylabel("Mean ADC code")
        ax.set_title(f"{title}: time-domain class means")
        ax.legend(loc="upper right", frameon=False, ncol=3, fontsize=8)
        style(ax)
    axes[-1].set_xlabel("Time from trigger (ms)")
    fig.tight_layout()
    fig.savefig(OUTPUT / "masked_vs_unmasked_time_domain_5k.png", dpi=300)
    fig.savefig(OUTPUT / "masked_vs_unmasked_time_domain_5k.pdf")
    plt.close(fig)

    summary = {}
    for name, result in (("masked", masked), ("unmasked", unmasked)):
        absolute = np.abs(result["t"])
        peak = int(np.argmax(absolute))
        summary[name] = {
            "total_traces": int(result["counts"].sum()),
            "class_0": int(result["counts"][0]),
            "class_1": int(result["counts"][1]),
            "max_abs_t": float(absolute[peak]),
            "peak_sample": int(result["sample_index"][peak]),
            "peak_time_ms": float(result["time_ms"][peak]),
            "samples_above_4_5": int((absolute > THRESHOLD).sum()),
        }
    (OUTPUT / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
