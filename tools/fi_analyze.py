#!/usr/bin/env python3
"""Summarize FI CSV files into detailed and aggregate Markdown tables."""

import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path


OUTCOMES = (
    "NO_EFFECT",
    "SAFE_REJECTION",
    "EXPLICIT_ERROR",
    "CRASH_RESET",
    "HANG_TIMEOUT",
    "SILENT_CORRUPTION",
    "AUTH_BYPASS",
    "WEAK_KEY",
    "INJECTION_MISSED",
)


def verdict(counts):
    if counts["AUTH_BYPASS"] or counts["WEAK_KEY"]:
        return "security vulnerable"
    if counts["SILENT_CORRUPTION"]:
        return "functional vulnerability"
    if counts["CRASH_RESET"] or counts["HANG_TIMEOUT"]:
        return "fail-stop/DoS observed"
    if counts["SAFE_REJECTION"] or counts["EXPLICIT_ERROR"]:
        return "protected in tested model"
    return "no observed effect"


def markdown_row(values):
    return "| " + " | ".join(str(value) for value in values) + " |"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", nargs="+", type=Path)
    parser.add_argument("-o", "--output", type=Path,
                        default=Path("fi_summary.md"))
    args = parser.parse_args()

    rows = []
    for path in args.csv:
        with path.open(newline="") as source:
            for row in csv.DictReader(source):
                outcome = row.get("outcome", "")
                if outcome not in OUTCOMES:
                    raise SystemExit(
                        "{}: unknown outcome {!r}".format(path, outcome))
                rows.append(row)

    detailed = defaultdict(Counter)
    aggregate = defaultdict(Counter)
    for row in rows:
        detail_key = (
            row["fault_model"], row["operation"], row["fault_id"],
            row["target"], row["size"],
        )
        detailed[detail_key][row["outcome"]] += 1
        aggregate[row["fault_model"]][row["outcome"]] += 1

    lines = [
        "# Fault-injection result summary",
        "",
        "## Detailed results",
        "",
        markdown_row([
            "Model", "Operation", "Fault ID", "Target", "Size", "Trials",
            "No effect", "Safe rejection", "Explicit error", "Crash/hang",
            "Silent", "Bypass", "Weak key", "Missed", "Verdict",
        ]),
        markdown_row(["---"] * 15),
    ]
    for key in sorted(detailed):
        counts = detailed[key]
        total = sum(counts.values())
        lines.append(markdown_row([
            *key, total, counts["NO_EFFECT"], counts["SAFE_REJECTION"],
            counts["EXPLICIT_ERROR"],
            counts["CRASH_RESET"] + counts["HANG_TIMEOUT"],
            counts["SILENT_CORRUPTION"], counts["AUTH_BYPASS"],
            counts["WEAK_KEY"], counts["INJECTION_MISSED"], verdict(counts),
        ]))

    lines.extend([
        "",
        "## Aggregate results",
        "",
        markdown_row([
            "Fault type", "Trials", "No effect", "Safe rejection",
            "Explicit error", "Crash/hang", "Silent", "Bypass", "Weak key",
            "Missed", "95% upper bound when no bypass/weak key",
        ]),
        markdown_row(["---"] * 11),
    ])
    for model in sorted(aggregate):
        counts = aggregate[model]
        total = sum(counts.values())
        dangerous = counts["AUTH_BYPASS"] + counts["WEAK_KEY"]
        bound = ("{:.3%}".format(min(1.0, 3.0 / total))
                 if total and not dangerous else "N/A")
        lines.append(markdown_row([
            model, total, counts["NO_EFFECT"], counts["SAFE_REJECTION"],
            counts["EXPLICIT_ERROR"],
            counts["CRASH_RESET"] + counts["HANG_TIMEOUT"],
            counts["SILENT_CORRUPTION"], counts["AUTH_BYPASS"],
            counts["WEAK_KEY"], counts["INJECTION_MISSED"], bound,
        ]))

    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("Wrote {} from {} trials".format(args.output, len(rows)))


if __name__ == "__main__":
    main()
