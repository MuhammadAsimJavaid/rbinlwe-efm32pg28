#!/usr/bin/env python3
"""Create an IEEE single-column figure from the four dudect capture logs."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from analyze import crop_class, parse_capture, running_t  # noqa: E402


LOGS = (
    ("dudect_CCA2_DEC01_asc.txt", "CCA2 dec."),
    ("dudect_CCA2_ENC01_asc.txt", "CCA2 enc."),
    ("dudect_decrypt_mul04_asc.txt", "Poly. mul. (dec.)"),
    ("dudect_encrypt_mul04_asc.txt", "Poly. mul. (enc.)"),
)
COLORS = ("#0072B2", "#E69F00", "#009E73", "#CC79A7")
LINESTYLES = ("-", "--", "-.", ":")
THRESHOLD_COLOR = "#B00020"
IEEE_COLUMN_WIDTH_IN = 3.5
IEEE_FIGURE_HEIGHT_IN = 1.95


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot four dudect convergence logs at IEEE column size."
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=Path.cwd(),
        help="directory containing the four dudect text logs",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("dudect_four_tests_t_convergence_ieee_column"),
        help="output path stem; .pdf and .png are created",
    )
    parser.add_argument("--crop-tail-percent", type=float, default=0.1)
    parser.add_argument("--checkpoint", type=int, default=1000)
    parser.add_argument("--threshold", type=float, default=4.5)
    parser.add_argument(
        "--height",
        type=float,
        default=IEEE_FIGURE_HEIGHT_IN,
        help=f"figure height in inches (default: {IEEE_FIGURE_HEIGHT_IN})",
    )
    return parser.parse_args()


def load_series(data_dir: Path, crop_tail_percent: float, checkpoint: int):
    series = []
    for filename, label in LOGS:
        capture = parse_capture(data_dir / filename)
        class0, _, _ = crop_class(capture.classes[0], crop_tail_percent)
        class1, _, _ = crop_class(capture.classes[1], crop_tail_percent)
        counts, statistics = running_t(class0, class1, checkpoint)
        absolute = [abs(value) for value in statistics]
        series.append((counts, absolute, label))
        print(
            f"{label}: final |t|={absolute[-1]:.6g}, "
            f"maximum |t|={max(absolute):.6g}"
        )
    return series


def save_plot(
    series, threshold: float, output: Path, height: float
) -> None:
    if height <= 0:
        raise ValueError("--height must be greater than zero")

    rc = {
        "font.family": "serif",
        "font.serif": ["Times New Roman", "Times", "DejaVu Serif"],
        "font.size": 8.0,
        "axes.labelsize": 8.0,
        "axes.titlesize": 8.0,
        "xtick.labelsize": 7.5,
        "ytick.labelsize": 7.5,
        "axes.linewidth": 0.65,
        "pdf.fonttype": 42,
        "ps.fonttype": 42,
    }
    with plt.rc_context(rc):
        fig, axis = plt.subplots(
            figsize=(IEEE_COLUMN_WIDTH_IN, height),
            constrained_layout=True,
        )
        for (counts, absolute, label), color, linestyle in zip(
            series, COLORS, LINESTYLES
        ):
            axis.plot(
                [count / 1000.0 for count in counts],
                absolute,
                color=color,
                linestyle=linestyle,
                linewidth=1.15,
                label=label,
                zorder=3,
            )

        axis.axhline(
            threshold,
            color=THRESHOLD_COLOR,
            linestyle=(0, (5, 2)),
            linewidth=0.9,
            zorder=2,
        )
        axis.text(
            0.985,
            threshold / 4.9 + 0.015,
            rf"threshold: $|t|={threshold:g}$",
            transform=axis.transAxes,
            color=THRESHOLD_COLOR,
            fontsize=7.5,
            ha="right",
            va="bottom",
        )
        axis.set_xlim(0, 51)
        axis.set_ylim(-0.12, 4.9)
        axis.set_xticks((0, 10, 20, 30, 40, 50))
        axis.set_yticks((0, 1, 2, 3, 4))
        axis.set_xlabel(r"Samples per class ($\times 10^3$)", labelpad=2)
        axis.set_ylabel(r"Running $|t|$", labelpad=2)
        axis.grid(True, linestyle=":", linewidth=0.45, alpha=0.35, zorder=0)
        axis.spines[["top", "right"]].set_visible(False)
        axis.tick_params(direction="out", width=0.65, length=2.6, pad=2)
        axis.legend(
            loc="upper center",
            bbox_to_anchor=(0.5, 0.80),
            ncol=2,
            frameon=False,
            fontsize=8.0,
            handlelength=2.2,
            handletextpad=0.4,
            columnspacing=0.7,
            labelspacing=0.45,
        )

        output.parent.mkdir(parents=True, exist_ok=True)
        stem = output.with_suffix("")
        fig.savefig(stem.with_suffix(".pdf"))
        fig.savefig(stem.with_suffix(".png"), dpi=600)
        plt.close(fig)


def main() -> int:
    args = arguments()
    try:
        series = load_series(
            args.data_dir, args.crop_tail_percent, args.checkpoint
        )
        save_plot(series, args.threshold, args.output, args.height)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    stem = args.output.with_suffix("")
    print(f"IEEE-column PDF: {stem.with_suffix('.pdf')}")
    print(f"IEEE-column PNG: {stem.with_suffix('.png')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
