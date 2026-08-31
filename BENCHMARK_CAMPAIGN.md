# Benchmark campaign

`tools/benchmark_campaign.py` automates build, flash, UART capture, cycle
summarization, and build-footprint logging for every selected parameter set.

## Combined cumulative campaign

The default `combined` campaign builds seven unique cumulative configurations.
`cpa_baseline` is the single shared boundary between baseline acceleration and
the later security stages; it is not rebuilt under a second name.

| Stage | Change introduced |
|---|---|
| `scalar_baseline` | scalar arithmetic, scalar codec, direct 4-byte SE TRNG calls |
| `packed_arithmetic` | add packed `UADD8`/`USUB8` arithmetic |
| `packed_codec` | add packed encoding/decoding |
| `cpa_baseline` | add buffered TRNG |
| `cpa_branch_free` | add branch-free binary multiplication |
| `cca2_unmasked` | add the unmasked CCA2 wrapper |
| `cca2_masked` | add fully masked secret/decryption processing |

Every stage emits the same three timing modes for KeyGen, encryption, and
decryption:

- `native`: latency presented by that build; a buffered build is prefilled
  outside the DWT interval, while a direct-TRNG build necessarily performs its
  TRNG calls inside the operation;
- `trng_only`: cost of generating exactly the random bytes used by the
  operation, with the build's configured 4-byte or 32-byte request pattern;
- `end_to_end`: the operation starts without prefetched randomness, so its
  cycle count always includes required randomness generation.

Use `end_to_end` for a consistent stage-to-stage comparison that includes
randomness cost. Use `native` to quantify the latency benefit delivered by
buffering.

Run the complete campaign (three parameter sets, seven stages, ten trials):

```powershell
python tools/benchmark_campaign.py --campaign combined --trials 10
```

`combined` is the default, and `all` is retained as an alias. The four-stage
subset remains available as `--campaign acceleration`; the legacy mode labels
remain available as `--campaign security` for reproducing older result files.
Use `--resume` with the same `--output-dir` to reuse configurations that
already have valid cycle and memory records.

## Logged output

Every build directory under `benchmark_results/<timestamp>/` contains:

- `results.csv`: raw per-trial cycles and status;
- `memory.csv`: build footprint for that exact configuration;
- `size_raw.txt`: original `arm-none-eabi-size` output;
- `firmware.map`: retained linker map;
- `firmware_sha256.txt`: identity of the flashed ELF;
- configure, build, flash, and UART logs.

Campaign-wide files are:

- `benchmark_summary.csv`: minimum, maximum, average, and median cycles;
- `benchmark_all_raw.csv`: all cycle samples;
- `memory_footprint.csv`: one footprint row per build.

The memory columns use GNU size section accounting. `flash_bytes` is
`text_bytes + data_bytes`. `ram_without_heap_bytes` sums all linked RAM
sections except the Silicon Labs auto-sized `.memory_manager_heap`, while
`linked_ram_bytes` includes that heap reservation. The report also separates
`.bss`, the reserved `.stack`, and `.memory_manager_heap`. This is a linked
build footprint, not a measured maximum stack depth; the retained map supports
detailed follow-up.
