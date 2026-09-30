#!/usr/bin/env python3
"""Resolve bounded native render-thread wall-time samples; run in WSL."""
import argparse
from collections import Counter
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("run", type=Path)
parser.add_argument("elf", type=Path)
parser.add_argument("--gl", action="store_true", help="Resolve GL consumer samples")
parser.add_argument("--callers", action="store_true", help="Resolve opt-in bounded GPU-owner caller chains")
args = parser.parse_args()
logs = sorted(args.run.glob("*-heap.log"), key=lambda p: int(p.name.split("-", 1)[0]))
log = next(p.read_text(errors="replace") for p in reversed(logs) if p.stat().st_size)
anchor = re.search(r"EDEN_PERF_PC_ANCHOR address=([0-9a-f]+)", log)
if not anchor:
    raise SystemExit("Missing runtime anchor; cannot resolve samples")
symbols = subprocess.check_output(["nm", "-n", "-C", "--defined-only", str(args.elf)], text=True)
symbol = next((line.split()[0] for line in symbols.splitlines()
               if "Eden::Performance::(anonymous namespace)::PcSignal(" in line), None)
if symbol is None:
    raise SystemExit("Missing matching sampler symbol; use the candidate's unstripped ELF")
bias = int(anchor[1], 16) - int(symbol, 16)
counts = Counter()
tag = "GL" if args.gl else "NATIVE"
for pc, count in re.findall(rf"EDEN_PERF_{tag}_PC mono_ns=\d+ pc=([0-9a-f]+) count=(\d+)", log):
    counts[int(pc, 16)] += int(count)
if not counts or 0 in counts:
    raise SystemExit("Missing or invalid instruction samples")
addresses = list(counts)
resolved = subprocess.check_output(["addr2line", "-f", "-C", "-e", str(args.elf),
                                   *(hex(pc - bias) for pc in addresses)], text=True).splitlines()
if len(resolved) != 2 * len(addresses):
    raise SystemExit("Unexpected symbolizer output")
totals, locations = Counter(), {}
for index, pc in enumerate(addresses):
    name, location = resolved[2 * index:2 * index + 2]
    if name == "??":
        name = f"unresolved/native-library PC {pc:x}"
    totals[name] += counts[pc]
    locations.setdefault(name, location)
total = sum(counts.values())
print(f"{args.run.name} {tag}: {total} wall-time samples; runtime/ELF bias {bias:#x}")
for name, count in totals.most_common(15):
    print(f"  {100 * count / total:5.1f}% {count:4d} {name[:150]} [{locations[name]}]")
print("Samples locate wall-time occupancy, not CPU cycles or an automatic speedup estimate.")

if args.callers:
    if args.gl:
        raise SystemExit("Caller-chain mode currently samples only the GPU owner")
    chains = Counter()
    for pc, count, chain in re.findall(
            r"EDEN_PERF_NATIVE_CALLERS mono_ns=\d+ pc=([0-9a-f]+) count=(\d+) callers=([0-9a-f,]+)", log):
        callers = tuple(int(x, 16) for x in chain.split(","))
        if len(callers) != 8:
            raise SystemExit("Malformed caller record; expected eight bounded entries")
        chains[(int(pc, 16), callers)] += int(count)
    if not chains or sum(chains.values()) != total:
        raise SystemExit("Missing/incomplete caller samples; no wait attribution available")
    addresses = sorted({pc - 1 for (_, chain) in chains for pc in chain if pc})
    lines = subprocess.check_output(["addr2line", "-f", "-C", "-e", str(args.elf),
                                    *(hex(pc - bias) for pc in addresses)], text=True).splitlines() if addresses else []
    if len(lines) != 2 * len(addresses):
        raise SystemExit("Unexpected caller symbolizer output")
    names = {pc + 1: lines[2*i] if lines[2*i] != "??" else f"unresolved/{pc+1:x}"
             for i, pc in enumerate(addresses)}
    resolved_chains = Counter()
    valid = 0
    for (pc, chain), count in chains.items():
        valid += count if chain[0] else 0
        resolved_chains[(pc, tuple(names[x] for x in chain if x))] += count
    print(f"Caller chains present: {valid}/{total}; missing chains are unknown, not idle")
    for (pc, chain), count in resolved_chains.most_common(12):
        print(f"  {100*count/total:5.1f}% {count:4d} PC {pc:x}: " +
              (" -> ".join(chain) if chain else "NO VALID CHAIN"))
    print("Frame chains may stop at omitted frames; these observations do not measure removable wait duration.")
