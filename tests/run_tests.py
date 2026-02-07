#!/usr/bin/env python3
"""
ANEM16 simulator test runner.

Assembles test programs, runs them through the simulator, and verifies
data memory contents against expected values.

Usage:
    python3 run_tests.py                    # run all tests
    python3 run_tests.py test_branches      # run specific test(s)
    python3 run_tests.py -v                 # verbose output
    python3 run_tests.py --no-assemble      # skip assembly step
"""

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ── Paths ──────────────────────────────────────────────────────────────

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
SIM_BIN = PROJECT_DIR / "builddir" / "sim"
ASSEMBLER = Path.home() / "work" / "anem16pipe" / "assembler" / "assembler.py"


# ── Test definitions ───────────────────────────────────────────────────
# Each test specifies:
#   name:     base filename (no extension) under tests/
#   memory:   dict of {addr: expected_value} to verify after execution
#   regs:     optional dict of {reg_number: expected_value} (not checked if omitted)

@dataclass
class TestCase:
    name: str
    memory: dict[int, int]
    regs: dict[int, int] = field(default_factory=dict)
    max_cycles: int = 10000
    known_fail: bool = False
    known_fail_reason: str = ""


TESTS = [
    TestCase(
        name="test_branches",
        memory={
            0: 0x0001,  # BZ.T taken when Z=1
            1: 0x0001,  # BZ.T fall-through when Z=0
            2: 0x0005,  # Loop counter result
            3: 0x0003,  # Backward branch accumulate
            4: 0x00AA,  # Forward branch over block
            5: 0x0001,  # BHLEQ taken when HI==LO
            6: 0x0001,  # BHLEQ not-taken when HI!=LO
        },
    ),
    TestCase(
        name="test_memory",
        memory={
            0: 0x0037,  # Basic SW/LW roundtrip
            1: 0x0037,  # LW with offset verification
            2: 0x0055,  # Store with offset
            3: 0x006E,  # Load-use forwarding (0x37+0x37)
            4: 0x003E,  # Load-modify-store (0x37+7)
            5: 0x0012,  # Address register indexing
            6: 0x00CC,  # Multiple store verification
        },
    ),
    TestCase(
        name="test_shifts",
        memory={
            0: 0x000E,  # SHL by 1 (7<<1)
            1: 0x0070,  # SHL by 4 (7<<4)
            2: 0x0003,  # SHR by 1 (7>>1)
            3: 0x0010,  # SHR by 4 (0x100>>4)
            4: 0x0010,  # SAR positive (0x100>>>4)
            5: 0xFFF0,  # SAR negative (0xFF00>>>4)
            6: 0x0001,  # ROL by 1 (0x8000 rotl 1)
            7: 0x8000,  # ROR by 1 (0x0001 rotr 1)
            8: 0x0007,  # SHL by 0 (unchanged)
            9: 0x0050,  # ROL by 4 (0x0005 rotl 4)
        },
    ),
    TestCase(
        name="test_fibonacci",
        memory={
            0: 0x0000,  # F(0)
            1: 0x0001,  # F(1)
            2: 0x0001,  # F(2)
            3: 0x0002,  # F(3)
            4: 0x0003,  # F(4)
            5: 0x0005,  # F(5)
            6: 0x0008,  # F(6)
            7: 0x000D,  # F(7)
            8: 0x0015,  # F(8)
            9: 0x0022,  # F(9)
        },
    ),
    TestCase(
        name="test_subroutine",
        memory={
            0: 0x0006,  # DOUBLE(3)
            1: 0x000F,  # TRIPLE(5)
            2: 0x001A,  # Nested: ADD_TEN then DOUBLE on 3
            3: 0x0001,  # Return address verification
        },
    ),
    TestCase(
        name="test_sort",
        memory={
            0: 0x0001,  # sorted[0]
            1: 0x0002,  # sorted[1]
            2: 0x0003,  # sorted[2]
            3: 0x0004,  # sorted[3]
            4: 0x0005,  # sorted[4]
        },
    ),
    TestCase(
        name="test_stack",
        memory={
            0: 0xFFCF,  # SPRD initial SP
            1: 0xFFCE,  # SP after PUSH
            2: 0x002A,  # POP value (42)
            3: 0xFFCF,  # SP after POP (restored)
            4: 0x0064,  # SPWR set SP=100
            5: 0xFFCF,  # SPWR restore SP
            6: 0xFFCC,  # SP after 3 back-to-back PUSHes
            7: 0x001E,  # POP 30 (LIFO order)
            8: 0x0014,  # POP 20
            9: 0x000A,  # POP 10
            10: 0x000C, # PUSH with ALU dependency (12)
            11: 0x0063, # POP load-use (99)
            12: 0x000F, # ADDI +5 (10+5=15)
            13: 0x000C, # ADDI -3 (15-3=12)
            14: 0x0019, # ADDI forwarding chain (12+7+6=25)
        },
    ),
    TestCase(
        name="test_hilo",
        memory={
            0: 0x1234,  # LHI + MFHI
            1: 0x5678,  # LLO + MFLO
            2: 0xAAAA,  # MTHI + MFHI
            3: 0xBBBB,  # MTLO + MFLO
            4: 0x002A,  # MUL 6*7
            5: 0x2710,  # MUL 100*100
            6: 0x0019,  # AIS 25
            7: 0x003C,  # AIS 10+20+30
        },
    ),
]


# ── Output parsing ─────────────────────────────────────────────────────

def parse_sim_output(output: str) -> tuple[dict[int, int], dict[int, int]]:
    """Parse simulator output, return (memory_dict, register_dict)."""
    memory = {}
    registers = {}

    # Parse registers: "R04 = 0x1234" or "PC = 0x0022"
    for m in re.finditer(r"R(\d{2})\s*=\s*0x([0-9a-fA-F]+)", output):
        reg_num = int(m.group(1))
        reg_val = int(m.group(2), 16)
        registers[reg_num] = reg_val

    # Parse HI/LO
    for m in re.finditer(r"(HI|LO)\s*=\s*0x([0-9a-fA-F]+)", output):
        name = m.group(1)
        val = int(m.group(2), 16)
        if name == "HI":
            registers[16] = val  # HI = pseudo-register 16
        else:
            registers[17] = val  # LO = pseudo-register 17

    # Parse memory: "[0004] = 0x002a"
    for m in re.finditer(r"\[(\d+)\]\s*=\s*0x([0-9a-fA-F]+)", output):
        addr = int(m.group(1))
        val = int(m.group(2), 16)
        memory[addr] = val

    return memory, registers


# ── Test execution ─────────────────────────────────────────────────────

def assemble(test: TestCase, verbose: bool) -> bool:
    """Assemble a test program. Returns True on success."""
    asm_file = SCRIPT_DIR / f"{test.name}.asm"
    if not asm_file.exists():
        print(f"  ASM file not found: {asm_file}")
        return False

    result = subprocess.run(
        [sys.executable, str(ASSEMBLER), test.name],
        cwd=str(SCRIPT_DIR),
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"  Assembler failed:\n{result.stderr}")
        return False

    if verbose:
        # Extract error/warning count from assembler output
        for line in result.stdout.splitlines():
            if "error" in line or "warning" in line:
                print(f"  Assembler: {line.strip()}")

    bin_file = SCRIPT_DIR / f"{test.name}.bin"
    if not bin_file.exists():
        print(f"  Binary not generated: {bin_file}")
        return False

    return True


def run_test(test: TestCase, verbose: bool) -> tuple[bool, list[str]]:
    """Run a single test. Returns (passed, list_of_failure_messages)."""
    bin_file = SCRIPT_DIR / f"{test.name}.bin"
    if not bin_file.exists():
        return False, [f"Binary not found: {bin_file}"]

    result = subprocess.run(
        [str(SIM_BIN), "-m", str(test.max_cycles), str(bin_file)],
        capture_output=True,
        text=True,
        timeout=30,
    )

    if result.returncode != 0:
        return False, [f"Simulator exited with code {result.returncode}: {result.stderr}"]

    memory, registers = parse_sim_output(result.stdout)
    failures = []

    # Check memory expectations
    for addr, expected in sorted(test.memory.items()):
        actual = memory.get(addr)
        if actual is None:
            failures.append(f"  mem[{addr}]: not in output (expected 0x{expected:04X})")
        elif actual != expected:
            failures.append(f"  mem[{addr}]: 0x{actual:04X} != expected 0x{expected:04X}")
        elif verbose:
            print(f"  mem[{addr}]: 0x{actual:04X} OK")

    # Check register expectations (if any)
    for reg, expected in sorted(test.regs.items()):
        actual = registers.get(reg)
        label = f"R{reg}" if reg < 16 else ("HI" if reg == 16 else "LO")
        if actual is None:
            failures.append(f"  {label}: not in output (expected 0x{expected:04X})")
        elif actual != expected:
            failures.append(f"  {label}: 0x{actual:04X} != expected 0x{expected:04X}")
        elif verbose:
            print(f"  {label}: 0x{actual:04X} OK")

    return len(failures) == 0, failures


# ── Main ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="ANEM16 simulator test runner")
    parser.add_argument("tests", nargs="*", help="Test names to run (default: all)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("--no-assemble", action="store_true",
                        help="Skip assembly step (use existing .bin files)")
    parser.add_argument("--assembler", type=str, default=None,
                        help="Path to assembler.py")
    parser.add_argument("--sim", type=str, default=None,
                        help="Path to simulator binary")
    args = parser.parse_args()

    global ASSEMBLER, SIM_BIN
    if args.assembler:
        ASSEMBLER = Path(args.assembler)
    if args.sim:
        SIM_BIN = Path(args.sim)

    # Validate tools exist
    if not SIM_BIN.exists():
        print(f"Error: simulator not found at {SIM_BIN}")
        print("  Build with: meson compile -C builddir")
        sys.exit(1)
    if not args.no_assemble and not ASSEMBLER.exists():
        print(f"Error: assembler not found at {ASSEMBLER}")
        print("  Use --no-assemble to skip assembly, or --assembler to specify path")
        sys.exit(1)

    # Select tests
    if args.tests:
        selected = []
        for name in args.tests:
            name = name.removesuffix(".asm").removesuffix(".bin")
            matches = [t for t in TESTS if t.name == name]
            if not matches:
                print(f"Unknown test: {name}")
                print(f"Available: {', '.join(t.name for t in TESTS)}")
                sys.exit(1)
            selected.extend(matches)
    else:
        selected = TESTS

    # Run tests
    passed = 0
    failed = 0
    xfailed = 0  # expected failures
    xpassed = 0  # unexpected passes
    errors = 0

    for test in selected:
        tag = ""
        if test.known_fail:
            tag = " [KNOWN FAIL]"
        print(f"--- {test.name}{tag} ---")

        # Assemble
        if not args.no_assemble:
            if not assemble(test, args.verbose):
                print(f"  SKIP (assembly failed)")
                errors += 1
                continue

        # Run
        try:
            ok, failures = run_test(test, args.verbose)
        except subprocess.TimeoutExpired:
            print(f"  TIMEOUT (>{test.max_cycles} cycles)")
            errors += 1
            continue
        except Exception as e:
            print(f"  ERROR: {e}")
            errors += 1
            continue

        if ok:
            if test.known_fail:
                print(f"  XPASS (unexpected pass!)")
                xpassed += 1
            else:
                print(f"  PASS")
                passed += 1
        else:
            for msg in failures:
                print(msg)
            if test.known_fail:
                print(f"  XFAIL ({test.known_fail_reason})")
                xfailed += 1
            else:
                print(f"  FAIL")
                failed += 1

    # Summary
    total = passed + failed + xfailed + xpassed + errors
    print(f"\n{'='*50}")
    print(f"Results: {total} tests")
    parts = []
    if passed:
        parts.append(f"{passed} passed")
    if failed:
        parts.append(f"{failed} FAILED")
    if xfailed:
        parts.append(f"{xfailed} expected failures")
    if xpassed:
        parts.append(f"{xpassed} unexpected passes")
    if errors:
        parts.append(f"{errors} errors")
    print(f"  {', '.join(parts)}")

    # Exit code: 0 if no unexpected failures or errors
    sys.exit(1 if (failed or errors) else 0)


if __name__ == "__main__":
    main()
