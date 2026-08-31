#!/usr/bin/env python3
"""Offline dudect-style analysis for the UART timing capture logs.

Welch's statistic is calculated as

                    mean_0 - mean_1
    t = -------------------------------------------
        sqrt(variance_0 / n_0 + variance_1 / n_1)

where each variance is the unbiased sample variance for its class.  Only the
absolute value is used for the dudect-style 4.5 reference threshold.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


START_RE = re.compile(r"^# start,([^,]+),(\d+)$")
DATA_RE = re.compile(r"^([01]),(\d+)$")
DEFAULT_THRESHOLD = 4.5


@dataclass(frozen=True)
class Capture:
    function_name: str | None
    expected_trials: int | None
    classes: tuple[list[float], list[float]]
    malformed_lines: int
    saw_end: bool


def parse_capture(path: Path) -> Capture:
    samples: tuple[list[float], list[float]] = ([], [])
    function_name: str | None = None
    expected_trials: int | None = None
    malformed = 0
    saw_end = False

    # Accept ordinary ASCII/UTF-8 captures and UTF-16 text exports from terminal
    # programs. Also tolerate exports that spell line endings as <CR><LF>
    # instead of storing actual CR and LF bytes.
    raw = path.read_bytes()
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        text = raw.decode("utf-16", errors="replace")
    else:
        text = raw.decode("utf-8-sig", errors="replace")
        if "\x00" in text:
            text = raw.decode("utf-16", errors="replace")

    text = re.sub(r"<CR>\s*<LF>", "\n", text, flags=re.IGNORECASE)

    # Replacement decoding permits recovery at the next line if a UART
    # fragment contains a damaged byte. Such a line is counted and ignored.
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue

        start_match = START_RE.fullmatch(line)
        if start_match:
            if function_name is not None:
                raise ValueError("log contains more than one start marker")
            function_name = start_match.group(1)
            expected_trials = int(start_match.group(2))
            continue

        if line == "# end":
            saw_end = True
            continue

        data_match = DATA_RE.fullmatch(line)
        if data_match:
            class_label = int(data_match.group(1))
            samples[class_label].append(float(data_match.group(2)))
            continue

        malformed += 1

    if not samples[0] or not samples[1]:
        raise ValueError(
            "the log must contain samples from both classes; recognized "
            f"class 0={len(samples[0])}, class 1={len(samples[1])}, "
            f"malformed={malformed}"
        )

    return Capture(function_name, expected_trials, samples, malformed, saw_end)


def percentile(values: Sequence[float], percentage: float) -> float:
    """Return a linearly interpolated percentile, equivalent to type-7."""
    if not values:
        raise ValueError("cannot calculate a percentile of an empty sequence")
    ordered = sorted(values)
    position = (len(ordered) - 1) * percentage / 100.0
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] + fraction * (ordered[upper] - ordered[lower])


def crop_class(values: Sequence[float], tail_percent: float) -> tuple[list[float], float, float]:
    """Crop tail_percent from both tails of one class independently."""
    low = percentile(values, tail_percent)
    high = percentile(values, 100.0 - tail_percent)
    return [value for value in values if low <= value <= high], low, high


def cumulative_sums(values: Iterable[float]) -> tuple[list[float], list[float]]:
    sums = [0.0]
    squared_sums = [0.0]
    for value in values:
        sums.append(sums[-1] + value)
        squared_sums.append(squared_sums[-1] + value * value)
    return sums, squared_sums


def welch_from_sums(
    n0: int,
    sum0: float,
    squares0: float,
    n1: int,
    sum1: float,
    squares1: float,
) -> float:
    if n0 < 2 or n1 < 2:
        return math.nan

    mean0 = sum0 / n0
    mean1 = sum1 / n1
    variance0 = (squares0 - sum0 * sum0 / n0) / (n0 - 1)
    variance1 = (squares1 - sum1 * sum1 / n1) / (n1 - 1)
    # Protect against tiny negative values caused by floating-point rounding.
    variance0 = max(variance0, 0.0)
    variance1 = max(variance1, 0.0)
    standard_error = math.sqrt(variance0 / n0 + variance1 / n1)

    if standard_error == 0.0:
        return 0.0 if mean0 == mean1 else math.copysign(math.inf, mean0 - mean1)
    return (mean0 - mean1) / standard_error


def running_t(
    class0: Sequence[float], class1: Sequence[float], checkpoint: int
) -> tuple[list[int], list[float]]:
    """Calculate t using the first n samples of each class at each checkpoint."""
    usable = min(len(class0), len(class1))
    if usable < 2:
        raise ValueError("at least two retained samples per class are required")

    sum0, squares0 = cumulative_sums(class0)
    sum1, squares1 = cumulative_sums(class1)
    counts = list(range(checkpoint, usable + 1, checkpoint))
    if not counts or counts[-1] != usable:
        counts.append(usable)

    statistics = [
        welch_from_sums(n, sum0[n], squares0[n], n, sum1[n], squares1[n])
        for n in counts
    ]
    return counts, statistics


def has_clear_upward_trend(abs_t: Sequence[float]) -> bool:
    """Flag a sustained late rise; this is a diagnostic, not a hypothesis test."""
    finite = [value for value in abs_t if math.isfinite(value)]
    if len(finite) < 8:
        return False
    half = len(finite) // 2
    late = finite[half:]
    x_mean = (len(late) - 1) / 2.0
    y_mean = sum(late) / len(late)
    denominator = sum((x - x_mean) ** 2 for x in range(len(late)))
    slope = sum(
        (x - x_mean) * (value - y_mean) for x, value in enumerate(late)
    ) / denominator
    quarter = max(1, len(late) // 4)
    earlier_median = percentile(late[-2 * quarter : -quarter], 50.0)
    final_median = percentile(late[-quarter:], 50.0)
    return slope * (len(late) - 1) > 1.0 and final_median > earlier_median


def save_plot(
    counts: Sequence[int],
    statistics: Sequence[float],
    threshold: float,
    output: Path,
    title: str,
) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise RuntimeError("matplotlib is required to create the PNG plot") from error

    abs_t = [abs(value) for value in statistics]
    fig, axis = plt.subplots(figsize=(9, 5.5))
    axis.plot(counts, abs_t, linewidth=1.4, label="Running |Welch t|")
    axis.axhline(
        threshold,
        color="tab:red",
        linestyle="--",
        linewidth=1.2,
        label=f"Reference threshold |t| = {threshold:g}",
    )
    axis.set_xlabel("Samples per class")
    axis.set_ylabel("|t|")
    axis.set_title(title)
    axis.grid(True, alpha=0.3)
    axis.legend()
    fig.tight_layout()
    fig.savefig(output, dpi=180)
    plt.close(fig)


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def tail_percentage(value: str) -> float:
    parsed = float(value)
    if not 0.0 <= parsed < 50.0:
        raise argparse.ArgumentTypeError("must be at least 0 and less than 50")
    return parsed


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Analyze a class,cycle UART log using running Welch t-tests."
    )
    parser.add_argument("csv", type=Path, help="UART CSV log to analyze")
    parser.add_argument(
        "--crop-tail-percent",
        type=tail_percentage,
        default=0.1,
        metavar="PERCENT",
        help=(
            "percentage cropped independently from each tail of each class "
            "(default: 0.1, retaining the central 99.8%%)"
        ),
    )
    parser.add_argument(
        "--checkpoint",
        type=positive_int,
        default=1000,
        help="increment in samples per class for running t (default: 1000)",
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=DEFAULT_THRESHOLD,
        help="absolute t reference threshold (default: 4.5)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="PNG path (default: <input-stem>_t_convergence.png)",
    )
    return parser.parse_args()


def main() -> int:
    args = arguments()
    output = args.output or args.csv.with_name(f"{args.csv.stem}_t_convergence.png")

    try:
        capture = parse_capture(args.csv)
        cropped0, low0, high0 = crop_class(
            capture.classes[0], args.crop_tail_percent
        )
        cropped1, low1, high1 = crop_class(
            capture.classes[1], args.crop_tail_percent
        )
        counts, statistics = running_t(cropped0, cropped1, args.checkpoint)
        abs_t = [abs(value) for value in statistics]
        max_abs_t = max(abs_t)
        upward_trend = has_clear_upward_trend(abs_t)
        received = len(capture.classes[0]) + len(capture.classes[1])
        framing_ok = (
            capture.function_name is not None
            and capture.expected_trials is not None
            and capture.saw_end
            and received == capture.expected_trials
        )
        passes = framing_ok and max_abs_t < args.threshold and not upward_trend

        name = capture.function_name or args.csv.stem
        save_plot(counts, statistics, args.threshold, output, f"{name}: t convergence")
    except (OSError, ValueError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    print(f"Function: {name}")
    print(f"Raw samples: class 0={len(capture.classes[0])}, class 1={len(capture.classes[1])}")
    print(
        f"Cropped samples: class 0={len(cropped0)} [{low0:g}, {high0:g}], "
        f"class 1={len(cropped1)} [{low1:g}, {high1:g}]"
    )
    print(f"Malformed lines ignored: {capture.malformed_lines}")
    print(f"Capture framing/count: {'OK' if framing_ok else 'INCOMPLETE'}")
    print(f"Final checkpoint |t|: {abs_t[-1]:.6g}")
    print(f"Maximum running |t|: {max_abs_t:.6g}")
    print(f"Clear upward trend: {'yes' if upward_trend else 'no'}")
    print(f"Result: {'PASS' if passes else 'FAIL'}")
    print(f"Plot: {output}")
    return 0 if passes else 1


if __name__ == "__main__":
    raise SystemExit(main())
