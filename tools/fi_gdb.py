"""GDB automation for the EFM32 CCA2-PKE logical fault-injection harness.

Load from an already connected GDB session:

    source tools/fi_gdb.py

Examples:

    fi-run --model none --operation decrypt-invalid --trials 10 --csv baseline.csv
    fi-run --model skip --operation decrypt-invalid --at *0x08001234 \
           --trials 100 --csv skip.csv
    fi-run --model zero --operation decrypt-invalid --at *0x08001234 \
           --target '&valid_mask' \
           --size 1 --trials 100 --csv zero.csv
    fi-run --model random --operation decrypt-invalid --at *0x08001234 \
           --register r3 \
           --size 4 --seed 1234 --trials 1000 --csv random.csv

The script intentionally does not choose security-critical addresses. Sites
must be selected from the exact linked ELF disassembly and recorded alongside
the results.
"""

import csv
import random
import shlex
import time

import gdb


MAGIC = 0x46495431
STATE_READY = 1
STATE_DONE = 3

OPERATIONS = {
    "decrypt-valid": 1,
    "decrypt-invalid": 2,
    "encrypt": 3,
    "keypair": 4,
}


def _u32(expression):
    return int(gdb.parse_and_eval(expression).cast(
        gdb.lookup_type("unsigned int")))


def _i32(expression):
    value = _u32(expression)
    return value - (1 << 32) if value & (1 << 31) else value


def _write_u32(expression, value):
    gdb.execute("set variable {} = {}".format(expression, value & 0xFFFFFFFF),
                to_string=True)


def _address(expression):
    return int(gdb.parse_and_eval(expression).cast(
        gdb.lookup_type("uintptr_t")))


def _read_bytes(expression, length):
    address = _address("&({})".format(expression))
    return bytes(gdb.selected_inferior().read_memory(address, length))


def _pc():
    return int(gdb.parse_and_eval("$pc"))


def _same_code_address(a, b):
    return (a & ~1) == (b & ~1)


def _instruction_length(address):
    instructions = gdb.selected_frame().architecture().disassemble(
        address, count=1)
    if len(instructions) != 1 or "length" not in instructions[0]:
        raise gdb.GdbError("GDB could not decode one instruction at 0x{:x}"
                           .format(address))
    return int(instructions[0]["length"])


def _classify(operation, stop_kind):
    if stop_kind != "complete":
        return stop_kind

    status = _i32("fi_result.api_status")
    correct = _u32("fi_result.output_is_correct")
    rejection = _u32("fi_result.output_is_rejection")
    candidate = _u32("fi_result.output_is_candidate")
    all_zero = _u32("fi_result.key_is_all_zero")

    if operation == "keypair":
        if status != 0:
            return "EXPLICIT_ERROR"
        if all_zero:
            return "WEAK_KEY"
        return "NO_EFFECT"

    if status != 0:
        return "EXPLICIT_ERROR"
    if operation == "decrypt-invalid":
        if rejection:
            return "SAFE_REJECTION"
        if candidate:
            return "AUTH_BYPASS"
        return "SILENT_CORRUPTION"
    if correct:
        return "NO_EFFECT"
    if rejection:
        return "SAFE_REJECTION"
    return "SILENT_CORRUPTION"


def _parse_arguments(text):
    tokens = shlex.split(text)
    options = {
        "model": None,
        "operation": None,
        "at": None,
        "target": None,
        "register": None,
        "size": 1,
        "trials": 1,
        "hit": 1,
        "seed": 1,
        "csv": "fi_results.csv",
        "fault_id": 0,
    }
    requires_value = {
        "--model": "model",
        "--operation": "operation",
        "--at": "at",
        "--target": "target",
        "--register": "register",
        "--size": "size",
        "--trials": "trials",
        "--hit": "hit",
        "--seed": "seed",
        "--csv": "csv",
        "--fault-id": "fault_id",
    }

    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token not in requires_value or index + 1 >= len(tokens):
            raise gdb.GdbError("unknown or incomplete option: {}".format(token))
        options[requires_value[token]] = tokens[index + 1]
        index += 2

    for name in ("size", "trials", "hit", "seed", "fault_id"):
        options[name] = int(options[name], 0)

    if options["model"] not in ("none", "skip", "zero", "random"):
        raise gdb.GdbError("--model must be none, skip, zero, or random")
    if options["operation"] not in OPERATIONS:
        raise gdb.GdbError("unknown --operation")
    if options["trials"] < 1:
        raise gdb.GdbError("--trials must be positive")
    if options["hit"] < 1:
        raise gdb.GdbError("--hit must be positive")
    if options["size"] not in (1, 2, 4):
        raise gdb.GdbError("--size must be 1, 2, or 4 bytes")
    if options["model"] != "none":
        if not options["at"]:
            raise gdb.GdbError("--at is required for fault injection")
    if options["model"] in ("zero", "random"):
        if bool(options["target"]) == bool(options["register"]):
            raise gdb.GdbError(
                "select exactly one of --target or --register")
    if options["model"] == "skip" and (
            options["target"] or options["register"]):
        raise gdb.GdbError("instruction skip uses --at only")
    return options


class FiRun(gdb.Command):
    """Run debugger-assisted fault-injection trials; use `help fi-run`."""

    def __init__(self):
        super().__init__("fi-run", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        del from_tty
        options = _parse_arguments(argument)
        self._run(options)

    @staticmethod
    def _reach_initial_ready():
        if _u32("fi_command.magic") != MAGIC:
            raise gdb.GdbError("FI protocol magic is missing; flash FI build")

        ready_address = _address("&fi_trial_ready")
        if (_u32("fi_result.state") in (STATE_READY, STATE_DONE)
                and _same_code_address(_pc(), ready_address)):
            return

        ready_breakpoint = gdb.Breakpoint(
            "fi_trial_ready", internal=True)
        gdb.execute("continue")
        if ready_breakpoint.is_valid():
            ready_breakpoint.delete()
        if not _same_code_address(_pc(), ready_address):
            raise gdb.GdbError("target stopped before fi_trial_ready")
        if _u32("fi_result.state") not in (STATE_READY, STATE_DONE):
            raise gdb.GdbError("FI harness setup failed; state={}"
                               .format(_u32("fi_result.state")))

    @staticmethod
    def _inject(options, generator):
        model = options["model"]
        injected_value = ""
        original_value = ""

        if model == "skip":
            address = _pc()
            length = _instruction_length(address)
            original_value = "pc=0x{:08x}".format(address)
            new_pc = address + length
            gdb.execute("set $pc = 0x{:x}".format(new_pc), to_string=True)
            injected_value = "pc=0x{:08x}".format(new_pc)
            return original_value, injected_value

        size = options["size"]
        mask = (1 << (8 * size)) - 1
        if options["register"]:
            register = options["register"].lstrip("$")
            original = int(gdb.parse_and_eval("$" + register)) & mask
            value = 0 if model == "zero" else generator.getrandbits(8 * size)
            if model == "random" and value == original:
                value ^= 1
            gdb.execute("set ${} = {}".format(register, value), to_string=True)
        else:
            address = _address(options["target"])
            inferior = gdb.selected_inferior()
            original_bytes = bytes(inferior.read_memory(address, size))
            original = int.from_bytes(original_bytes, "little")
            value = 0 if model == "zero" else generator.getrandbits(8 * size)
            if model == "random" and value == original:
                value ^= 1
            inferior.write_memory(address, value.to_bytes(size, "little"))

        original_value = "0x{:0{}x}".format(original, size * 2)
        injected_value = "0x{:0{}x}".format(value, size * 2)
        return original_value, injected_value

    def _run(self, options):
        self._reach_initial_ready()
        ready_address = _address("&fi_trial_ready")
        generator = random.Random(options["seed"])
        fields = [
            "trial_id", "timestamp", "operation", "fault_model", "fault_id",
            "injection_at", "target", "size", "original_value", "injected_value",
            "stop_pc", "api_status", "valid_mask", "output_is_correct",
            "output_is_rejection", "output_is_candidate",
            "secret_hamming_weight", "outcome", "output_hex",
        ]

        with open(options["csv"], "w", newline="") as output_file:
            writer = csv.DictWriter(output_file, fieldnames=fields)
            writer.writeheader()

            for trial in range(options["trials"]):
                _write_u32("fi_command.operation",
                           OPERATIONS[options["operation"]])
                _write_u32("fi_command.trial_id", trial)
                _write_u32("fi_command.fault_id", options["fault_id"])
                _write_u32("fi_command.fault_seed", options["seed"])

                injection_breakpoint = None
                if options["model"] != "none":
                    injection_breakpoint = gdb.Breakpoint(
                        options["at"], internal=True)
                ready_breakpoint = gdb.Breakpoint(
                    "fi_trial_ready", internal=True)

                _write_u32("fi_command.command", 1)
                gdb.execute("continue")

                original_value = ""
                injected_value = ""
                stop_kind = "complete"
                for _ in range(1, options["hit"]):
                    if _same_code_address(_pc(), ready_address):
                        stop_kind = "INJECTION_MISSED"
                        break
                    gdb.execute("continue")
                if (injection_breakpoint is not None
                        and stop_kind == "complete"
                        and not _same_code_address(_pc(), ready_address)):
                    original_value, injected_value = self._inject(
                        options, generator)
                    if injection_breakpoint.is_valid():
                        injection_breakpoint.delete()
                    gdb.execute("continue")

                elif (injection_breakpoint is not None
                      and _same_code_address(_pc(), ready_address)):
                    stop_kind = "INJECTION_MISSED"

                if (stop_kind == "complete"
                        and not _same_code_address(_pc(), ready_address)):
                    stop_kind = "CRASH_RESET"
                elif (stop_kind == "complete"
                      and _u32("fi_result.state") != STATE_DONE):
                    stop_kind = "HANG_TIMEOUT"

                if ready_breakpoint.is_valid():
                    ready_breakpoint.delete()
                if (injection_breakpoint is not None
                        and injection_breakpoint.is_valid()):
                    injection_breakpoint.delete()

                length = int(gdb.parse_and_eval("sizeof(fi_result.output)"))
                output_bytes = _read_bytes("fi_result.output", length)
                outcome = _classify(options["operation"], stop_kind)
                writer.writerow({
                    "trial_id": trial,
                    "timestamp": int(time.time()),
                    "operation": options["operation"],
                    "fault_model": options["model"],
                    "fault_id": options["fault_id"],
                    "injection_at": options["at"] or "",
                    "target": options["target"] or options["register"] or "",
                    "size": options["size"],
                    "original_value": original_value,
                    "injected_value": injected_value,
                    "stop_pc": "0x{:08x}".format(_pc()),
                    "api_status": _i32("fi_result.api_status"),
                    "valid_mask": "0x{:02x}".format(
                        _u32("fi_result.valid_mask") & 0xFF),
                    "output_is_correct":
                        _u32("fi_result.output_is_correct"),
                    "output_is_rejection":
                        _u32("fi_result.output_is_rejection"),
                    "output_is_candidate":
                        _u32("fi_result.output_is_candidate"),
                    "secret_hamming_weight":
                        _u32("fi_result.secret_hamming_weight"),
                    "outcome": outcome,
                    "output_hex": output_bytes.hex(),
                })
                output_file.flush()
                gdb.write("trial {}/{}: {}\n".format(
                    trial + 1, options["trials"], outcome))


FiRun()
gdb.write("Loaded FI command. Use `help fi-run` and tools/fi_gdb.py examples.\n")
