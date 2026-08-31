#!/usr/bin/env python3
"""Generate ordinary GDB command files for GDB builds without Python support."""

import argparse
import random
import shlex
from pathlib import Path


OPERATIONS = {
    "decrypt-valid": 1,
    "decrypt-invalid": 2,
    "encrypt": 3,
    "keypair": 4,
}


def q(text):
    """Quote a string for a GDB printf field."""
    return text.replace("\\", "/").replace('"', '""')


def outcome_commands(operation):
    if operation == "keypair":
        return [
            "if fi_result.api_status != 0",
            '  printf "EXPLICIT_ERROR"',
            "else",
            "  if fi_result.key_is_all_zero",
            '    printf "WEAK_KEY"',
            "  else",
            '    printf "NO_EFFECT"',
            "  end",
            "end",
        ]
    if operation == "decrypt-invalid":
        return [
            "if fi_result.api_status != 0",
            '  printf "EXPLICIT_ERROR"',
            "else",
            "  if fi_result.candidate_bits_released != 0",
            '    printf "AUTH_BYPASS"',
            "  else",
            "    if fi_result.output_is_rejection",
            '      printf "SAFE_REJECTION"',
            "    else",
            "      if fi_result.output_is_candidate",
            '        printf "AUTH_BYPASS"',
            "      else",
            '        printf "SILENT_CORRUPTION"',
            "      end",
            "    end",
            "  end",
            "end",
        ]
    return [
        "if fi_result.api_status != 0",
        '  printf "EXPLICIT_ERROR"',
        "else",
        "  if fi_result.output_is_correct",
        '    printf "NO_EFFECT"',
        "  else",
        "    if fi_result.output_is_rejection",
        '      printf "SAFE_REJECTION"',
        "    else",
        '      printf "SILENT_CORRUPTION"',
        "    end",
        "  end",
        "end",
    ]


def injection_commands(args, value):
    if args.model == "none":
        return []

    lines = [f"tbreak {args.at}", "commands", "  silent"]
    if args.model == "skip":
        lines.append(f"  set $pc = {args.next}")
    elif args.register:
        lines.append(f"  set ${args.register.lstrip('$')} = {value}")
    else:
        remaining = args.size
        offset = 0
        while remaining:
            width = 4 if remaining >= 4 else (2 if remaining >= 2 else 1)
            ctype = {
                1: "unsigned char",
                2: "unsigned short",
                4: "unsigned int",
            }[width]
            part_mask = (1 << (8 * width)) - 1
            part_value = (value >> (8 * offset)) & part_mask
            lines.append(
                f"  set {{{ctype}}} (({args.target}) + {offset}) = "
                f"{part_value}")
            remaining -= width
            offset += width
    lines.extend(["  continue", "end"])
    if args.hit > 1:
        lines.append(f"ignore $bpnum {args.hit - 1}")
    return lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True,
                        choices=("none", "skip", "zero", "random"))
    parser.add_argument("--operation", required=True, choices=OPERATIONS)
    parser.add_argument("--trials", type=int, default=1)
    parser.add_argument("--fault-id", type=int, default=0)
    parser.add_argument("--at", help="injection breakpoint, e.g. *0x08001234")
    parser.add_argument("--next", help="decoded next address for skip")
    parser.add_argument("--target", help="memory address expression")
    parser.add_argument("--register", help="register name, e.g. r3")
    parser.add_argument("--size", type=int, default=1)
    parser.add_argument("--hit", type=int, default=1)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument(
        "--value-mode", choices=("random", "exhaustive-byte"),
        default="random",
        help="random values from --seed, or the complete byte set 0..255")
    parser.add_argument("--script", type=Path, required=True)
    parser.add_argument(
        "--csv", type=Path,
        help="deprecated; standalone runner selects the output CSV path")
    args = parser.parse_args()

    if args.trials < 1 or args.hit < 1:
        parser.error("--trials and --hit must be positive")
    if args.size < 1:
        parser.error("--size must be positive")
    if args.model != "none" and not args.at:
        parser.error("--at is required for injected trials")
    if args.model == "skip" and not args.next:
        parser.error("--next is required for instruction skip")
    if args.model == "skip" and args.next.lstrip().startswith("*"):
        parser.error(
            "--next must be a code address without '*'; '*' dereferences it")
    if args.model in ("zero", "random"):
        if bool(args.target) == bool(args.register):
            parser.error("select exactly one of --target or --register")
    if args.register and args.size not in (1, 2, 4):
        parser.error("register corruption size must be 1, 2, or 4")
    if args.model == "random" and args.size > 4:
        parser.error("random memory corruption currently supports up to 4 bytes")
    if args.value_mode == "exhaustive-byte":
        if args.model != "random" or args.size != 1:
            parser.error(
                "--value-mode exhaustive-byte requires --model random --size 1")
        if args.trials != 256:
            parser.error(
                "--value-mode exhaustive-byte requires --trials 256")

    generator = random.Random(args.seed)
    lines = [
        "set pagination off",
        "set confirm off",
        ('printf "FIHEADER,trial_id,timestamp,operation,fault_model,fault_id,'
         'injection_at,target,size,original_value,injected_value,stop_pc,'
         'api_status,valid_mask,output_is_correct,output_is_rejection,'
         'output_is_candidate,secret_hamming_weight,'
         'candidate_differing_bits,candidate_bits_released,'
         'rejection_bits_retained,partial_candidate_release,'
         'outcome,output_hex\\n"'),
    ]

    mask = (1 << (8 * args.size)) - 1
    for trial in range(args.trials):
        value = 0
        if args.model == "random" and args.value_mode == "exhaustive-byte":
            value = trial
        elif args.model == "random":
            value = generator.getrandbits(8 * args.size) & mask

        lines.extend([
            f"set variable fi_command.operation = {OPERATIONS[args.operation]}",
            f"set variable fi_command.trial_id = {trial}",
            f"set variable fi_command.fault_id = {args.fault_id}",
            f"set variable fi_command.fault_seed = {args.seed}",
        ])
        lines.extend(injection_commands(args, value))
        lines.extend([
            "tbreak fi_trial_ready",
            "set variable fi_command.command = 1",
            "continue",
            (
                f'printf "FIROW,{trial},0,{q(args.operation)},{q(args.model)},'
                f'{args.fault_id},{q(args.at or "")},'
                f'{q(args.target or args.register or "")},{args.size},,'
                f'{value if args.model in ("zero", "random") else ""},'
                '0x%x,%d,0x%x,%u,%u,%u,%u,%u,%u,%u,%u,", $pc, '
                "fi_result.api_status, fi_result.valid_mask, "
                "fi_result.output_is_correct, "
                "fi_result.output_is_rejection, "
                "fi_result.output_is_candidate, "
                "fi_result.secret_hamming_weight, "
                "fi_result.candidate_differing_bits, "
                "fi_result.candidate_bits_released, "
                "fi_result.rejection_bits_retained, "
                "fi_result.partial_candidate_release"
            ),
        ])
        lines.extend(outcome_commands(args.operation))
        lines.append('printf ",\\n"')

    lines.append('printf "FICOMPLETE,%u\\n", fi_result.completed_trial_id')
    args.script.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Wrote {args.script} for {args.trials} trials")
    print(f"GDB command: source {args.script.resolve().as_posix()}")


if __name__ == "__main__":
    main()
