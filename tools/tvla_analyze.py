"""First- and second-order fixed-vs-random TVLA for trace captures."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import numpy as np


THRESHOLD = 4.5
def load_log(path: Path, trace_count: int, point_count: int):
    records: list[tuple[int, int, int]] = []
    rejected = 0
    seen: set[int] = set()
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            index = int(row["trace_index"])
            good = (
                row["status"].startswith("ok")
                and int(row["samples"]) == point_count
                and 0 <= index < trace_count
                and index not in seen
            )
            if not good:
                rejected += 1
                continue
            seen.add(index)
            records.append(
                (index, int(row["class"]), int(row["trigger_edge_index"]))
            )
    if not records:
        raise RuntimeError("capture_log.csv contains no valid completed traces")
    records.sort()
    values = np.asarray(records, dtype=np.int64)
    return values[:, 0], values[:, 1], values[:, 2], rejected


def accumulate(traces, rows, labels, edges, target_edge, lo, hi):
    width = hi - lo
    sums = [np.zeros(width, dtype=np.float64) for _ in range(2)]
    sums2 = [np.zeros(width, dtype=np.float64) for _ in range(2)]
    counts = [0, 0]

    # Traces sharing an edge displacement can be read as one efficient block.
    deltas = edges - target_edge
    for label in (0, 1):
        for delta in np.unique(deltas[labels == label]):
            select = (labels == label) & (deltas == delta)
            selected_rows = rows[select]
            start = lo + int(delta)
            stop = hi + int(delta)
            block = np.asarray(
                traces[selected_rows, start:stop], dtype=np.float64
            )
            sums[label] += block.sum(axis=0, dtype=np.float64)
            sums2[label] += np.square(block).sum(axis=0, dtype=np.float64)
            counts[label] += block.shape[0]
    return sums, sums2, counts


def accumulate_centered_square(
    traces, rows, labels, edges, target_edge, lo, hi, center
):
    width = hi - lo
    sums = [np.zeros(width, dtype=np.float64) for _ in range(2)]
    sums2 = [np.zeros(width, dtype=np.float64) for _ in range(2)]
    counts = [0, 0]
    deltas = edges - target_edge

    for label in (0, 1):
        for delta in np.unique(deltas[labels == label]):
            selected_rows = rows[(labels == label) & (deltas == delta)]
            start = lo + int(delta)
            stop = hi + int(delta)
            for offset in range(0, len(selected_rows), 256):
                batch_rows = selected_rows[offset : offset + 256]
                block = np.asarray(
                    traces[batch_rows, start:stop], dtype=np.float64
                )
                transformed = np.square(block - center)
                sums[label] += transformed.sum(axis=0, dtype=np.float64)
                sums2[label] += np.square(transformed).sum(
                    axis=0, dtype=np.float64
                )
                counts[label] += block.shape[0]
    return sums, sums2, counts


def welch_t(sums, sums2, counts):
    means = [sums[c] / counts[c] for c in (0, 1)]
    variances = [
        np.maximum(
            (sums2[c] - sums[c] * sums[c] / counts[c]) / (counts[c] - 1),
            0.0,
        )
        for c in (0, 1)
    ]
    denominator = np.sqrt(
        variances[0] / counts[0] + variances[1] / counts[1]
    )
    values = np.divide(
        means[0] - means[1], denominator,
        out=np.zeros_like(denominator), where=denominator > 0,
    )
    return values, means


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Analyze interrupted or completed tvla_collect.py output"
    )
    parser.add_argument("capture_dir", type=Path)
    parser.add_argument("--threshold", type=float, default=THRESHOLD)
    parser.add_argument(
        "--max-edge-deviation",
        type=int,
        default=250,
        help="reject trigger edges farther than this many samples from median",
    )
    parser.add_argument("--start-sample", type=int,
                        help="first predetermined sample to analyze")
    parser.add_argument("--stop-sample", type=int,
                        help="exclusive predetermined sample to analyze")
    parser.add_argument(
        "--sample-rate",
        type=float,
        help="sample rate in Sa/s; required only if metadata.json is absent",
    )
    parser.add_argument(
        "--hardware-trigger-index",
        type=int,
        help="trigger sample index for bulk captures that logged edge index zero",
    )
    args = parser.parse_args()

    capture_dir = args.capture_dir.resolve()
    metadata_path = capture_dir / "metadata.json"
    metadata: dict = {}
    if metadata_path.exists():
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        sample_interval_s = float(metadata["sample_interval_s"])
    elif args.sample_rate:
        sample_interval_s = 1.0 / args.sample_rate
    else:
        raise RuntimeError(
            "metadata.json is absent; specify --sample-rate for an "
            "interrupted capture"
        )
    trace_candidates = []
    if metadata.get("trace_file"):
        trace_candidates.append(capture_dir / str(metadata["trace_file"]))
    trace_candidates.extend(
        (capture_dir / "traces_int8.npy", capture_dir / "traces_int16.npy")
    )
    trace_path = next((path for path in trace_candidates if path.exists()), None)
    if trace_path is None:
        raise FileNotFoundError(
            "capture directory contains neither traces_int8.npy nor "
            "traces_int16.npy"
        )
    traces = np.load(trace_path, mmap_mode="r")
    if traces.ndim != 2:
        raise RuntimeError(f"unexpected trace array shape: {traces.shape}")
    rows, labels, edges, rejected = load_log(
        capture_dir / "capture_log.csv", traces.shape[0], traces.shape[1]
    )
    if args.hardware_trigger_index is not None:
        edges = np.full_like(edges, args.hardware_trigger_index)
    if not np.all(np.isin(labels, (0, 1))):
        raise RuntimeError("capture log contains a class other than 0 or 1")

    target_edge = int(np.median(edges))
    edge_ok = np.abs(edges - target_edge) <= args.max_edge_deviation
    edge_outliers = int((~edge_ok).sum())
    rows, labels, edges = rows[edge_ok], labels[edge_ok], edges[edge_ok]
    if len(rows) == 0:
        raise RuntimeError("all completed traces failed the trigger-edge check")
    deltas = edges - target_edge
    # Use only the overlap present in every shifted trace; never zero-pad.
    lo = max(0, int(-deltas.min()))
    hi = min(traces.shape[1], int(traces.shape[1] - deltas.max()))
    if args.start_sample is not None:
        lo = max(lo, args.start_sample)
    if args.stop_sample is not None:
        hi = min(hi, args.stop_sample)
    if hi <= lo:
        raise RuntimeError("trigger-edge shifts leave no common trace interval")

    sums, sums2, counts = accumulate(
        traces, rows, labels, edges, target_edge, lo, hi
    )
    if min(counts) < 2:
        raise RuntimeError(f"need at least two traces per class; got {counts}")

    t_value, means = welch_t(sums, sums2, counts)
    global_center = (sums[0] + sums[1]) / sum(counts)
    second_sums, second_sums2, second_counts = accumulate_centered_square(
        traces, rows, labels, edges, target_edge, lo, hi, global_center
    )
    second_t, _ = welch_t(second_sums, second_sums2, second_counts)
    sample_index = np.arange(lo, hi, dtype=np.int64)
    time_from_trigger_s = (sample_index - target_edge) * sample_interval_s
    absolute_t = np.abs(t_value)
    peak = int(np.argmax(absolute_t))
    exceeding = absolute_t > args.threshold
    second_absolute_t = np.abs(second_t)
    second_peak = int(np.argmax(second_absolute_t))
    second_exceeding = second_absolute_t > args.threshold

    output_npz = capture_dir / "tvla_first_order.npz"
    np.savez_compressed(
        output_npz,
        t_value=t_value,
        time_from_trigger_s=time_from_trigger_s,
        sample_index=sample_index,
        mean_class_0=means[0],
        mean_class_1=means[1],
        count_class_0=np.int64(counts[0]),
        count_class_1=np.int64(counts[1]),
        threshold=np.float64(args.threshold),
    )
    second_output_npz = capture_dir / "tvla_second_order.npz"
    np.savez_compressed(
        second_output_npz,
        t_value=second_t,
        time_from_trigger_s=time_from_trigger_s,
        sample_index=sample_index,
        count_class_0=np.int64(second_counts[0]),
        count_class_1=np.int64(second_counts[1]),
        threshold=np.float64(args.threshold),
        centering="pooled_sample_mean",
    )

    summary = {
        "capture_directory": str(capture_dir),
        "completed_valid_traces": int(len(rows)),
        "rejected_log_rows": int(rejected),
        "rejected_trigger_edge_outliers": edge_outliers,
        "class_0": int(counts[0]),
        "class_1": int(counts[1]),
        "alignment_target_edge": target_edge,
        "edge_min": int(edges.min()),
        "edge_max": int(edges.max()),
        "analyzed_samples": int(len(t_value)),
        "threshold": float(args.threshold),
        "first_order_max_abs_t": float(absolute_t[peak]),
        "first_order_peak_sample_index": int(sample_index[peak]),
        "first_order_peak_time_from_trigger_s": float(time_from_trigger_s[peak]),
        "first_order_samples_above_threshold": int(exceeding.sum()),
        "first_order_threshold_exceeded": bool(exceeding.any()),
        "second_order_max_abs_t": float(second_absolute_t[second_peak]),
        "second_order_peak_sample_index": int(sample_index[second_peak]),
        "second_order_peak_time_from_trigger_s": float(time_from_trigger_s[second_peak]),
        "second_order_samples_above_threshold": int(second_exceeding.sum()),
        "second_order_threshold_exceeded": bool(second_exceeding.any()),
        "first_order_result_file": str(output_npz),
        "second_order_result_file": str(second_output_npz),
    }
    (capture_dir / "tvla_summary.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )
    print(json.dumps(summary, indent=2))

    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig, axis = plt.subplots(figsize=(12, 4.5))
        axis.plot(time_from_trigger_s * 1e3, t_value, linewidth=0.7)
        axis.axhline(args.threshold, color="red", linestyle="--", linewidth=1)
        axis.axhline(-args.threshold, color="red", linestyle="--", linewidth=1)
        axis.set(xlabel="Time from PD12 rising edge (ms)", ylabel="Welch t-value")
        axis.set_title(
            f"First-order TVLA: class 0 ({counts[0]}) vs class 1 ({counts[1]})"
        )
        axis.grid(alpha=0.25)
        fig.tight_layout()
        plot_path = capture_dir / "tvla_first_order.png"
        fig.savefig(plot_path, dpi=160)
        print(f"plot: {plot_path}")

        fig, axis = plt.subplots(figsize=(12, 4.5))
        axis.plot(time_from_trigger_s * 1e3, second_t, linewidth=0.7)
        axis.axhline(args.threshold, color="red", linestyle="--", linewidth=1)
        axis.axhline(-args.threshold, color="red", linestyle="--", linewidth=1)
        axis.set(xlabel="Time from PD12 rising edge (ms)",
                 ylabel="Welch t-value")
        axis.set_title(
            f"Second-order TVLA: class 0 ({counts[0]}) vs class 1 ({counts[1]})"
        )
        axis.grid(alpha=0.25)
        fig.tight_layout()
        second_plot_path = capture_dir / "tvla_second_order.png"
        fig.savefig(second_plot_path, dpi=160)
        print(f"plot: {second_plot_path}")
    except ImportError:
        print("matplotlib is not installed; numerical TVLA results were saved")


if __name__ == "__main__":
    main()
