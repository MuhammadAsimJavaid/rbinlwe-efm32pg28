#!/usr/bin/env python3
"""Read one line from a serial port (small bring-up diagnostic)."""

from __future__ import annotations

import argparse
import sys

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=args.timeout) as uart:
        uart.reset_input_buffer()
        print(f"Listening on {args.port}...", flush=True)
        data = uart.readline()

    if not data:
        print("No UART data received before timeout.", file=sys.stderr)
        return 2

    print(data.decode("utf-8", errors="replace"), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
