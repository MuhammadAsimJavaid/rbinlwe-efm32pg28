#!/usr/bin/env python3
"""Extract clean FI CSV rows from a GDB/MI logging transcript."""

import argparse
import csv
import json
import re
from pathlib import Path


HEADER_PREFIX = "trial_id,timestamp,operation,fault_model,"
ROW_PREFIX = re.compile(r"^[0-9]+,")
OUTCOMES = {
    "NO_EFFECT",
    "SAFE_REJECTION",
    "EXPLICIT_ERROR",
    "CRASH_RESET",
    "HANG_TIMEOUT",
    "SILENT_CORRUPTION",
    "AUTH_BYPASS",
    "WEAK_KEY",
    "INJECTION_MISSED",
}


def decode_console_record(line):
    line = line.strip()
    if not line.startswith('~"'):
        return None
    try:
        return json.loads(line[1:])
    except json.JSONDecodeError:
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("transcript", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()

    header = None
    rows = []
    partial = None

    with args.transcript.open(encoding="utf-8", errors="replace") as source:
        for raw_line in source:
            text = decode_console_record(raw_line)
            if text is None:
                continue
            if text.startswith(HEADER_PREFIX):
                header = text.strip()
            elif ROW_PREFIX.match(text):
                partial = text
            elif partial is not None and text in OUTCOMES:
                partial += text
            elif partial is not None and text == ",\n":
                rows.append(partial + ",")
                partial = None

    if header is None:
        raise SystemExit("FI CSV header was not found in the MI transcript")
    if partial is not None:
        raise SystemExit("MI transcript ended during an FI result row")
    if not rows:
        raise SystemExit("no FI result rows were found")

    # Parse and rewrite rather than copying strings so malformed field counts
    # are detected before the data reaches the statistical analysis.
    header_fields = next(csv.reader([header]))
    parsed_rows = [next(csv.reader([row])) for row in rows]
    for index, row in enumerate(parsed_rows):
        if len(row) != len(header_fields):
            raise SystemExit(
                "row {} has {} fields; expected {}".format(
                    index, len(row), len(header_fields)))

    with args.output.open("w", newline="", encoding="utf-8") as destination:
        writer = csv.writer(destination)
        writer.writerow(header_fields)
        writer.writerows(parsed_rows)

    print("Extracted {} clean FI rows to {}".format(len(rows), args.output))


if __name__ == "__main__":
    main()
