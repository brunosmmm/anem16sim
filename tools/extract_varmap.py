#!/usr/bin/env python3
"""Extract variable-to-register/stack mappings from DWARF debug info.

Usage: extract_varmap.py <elf_file> [--dwarfdump PATH]

Runs llvm-dwarfdump on the ELF file and parses DW_TAG_variable and
DW_TAG_formal_parameter entries to produce a .varmap file.

Output format (one line per variable mapping):
  start_pc end_pc name location
  0x0004 0x0020 count R3
  0x0002 0x0010 x R1
  0x0000 0x0030 arr stack+4
"""

import subprocess
import sys
import re
import os


def parse_dwarfdump(text):
    """Parse llvm-dwarfdump --debug-info output into variable mappings."""
    mappings = []

    # Track current function's PC range
    func_low_pc = None
    func_high_pc = None
    current_tag = None
    current_name = None
    current_location = None

    for line in text.splitlines():
        stripped = line.strip()

        # Track subprogram (function) PC ranges
        if 'DW_TAG_subprogram' in stripped:
            func_low_pc = None
            func_high_pc = None
            current_tag = 'subprogram'
            continue

        # Track variable/parameter tags
        if 'DW_TAG_variable' in stripped or 'DW_TAG_formal_parameter' in stripped:
            current_tag = 'variable'
            current_name = None
            current_location = None
            continue

        # Other tags reset current
        if re.match(r'0x[0-9a-f]+:\s+DW_TAG_', stripped):
            if current_tag == 'variable' and current_name and current_location:
                for loc in current_location:
                    mappings.append(loc)
            current_tag = None
            current_name = None
            current_location = None
            continue

        # NULL tag (end of children) — flush pending
        if stripped == 'NULL':
            if current_tag == 'variable' and current_name and current_location:
                for loc in current_location:
                    mappings.append(loc)
            current_tag = None
            current_name = None
            current_location = None
            continue

        # Parse attributes
        if current_tag == 'subprogram':
            m = re.search(r'DW_AT_low_pc\s+\((0x[0-9a-f]+)\)', stripped)
            if m:
                func_low_pc = int(m.group(1), 16)
            m = re.search(r'DW_AT_high_pc\s+\((0x[0-9a-f]+)\)', stripped)
            if m:
                func_high_pc = int(m.group(1), 16)
            # high_pc as offset
            m = re.search(r'DW_AT_high_pc\s+\((\d+)\)', stripped)
            if m and func_low_pc is not None:
                func_high_pc = func_low_pc + int(m.group(1))

        if current_tag == 'variable':
            # Variable name
            m = re.search(r'DW_AT_name\s+\("([^"]+)"\)', stripped)
            if m:
                current_name = m.group(1)

            # Simple register location: DW_OP_reg<N>
            m = re.search(r'DW_AT_location\s+\(DW_OP_reg(\d+)\)', stripped)
            if m:
                reg = int(m.group(1))
                start = func_low_pc if func_low_pc is not None else 0
                end = func_high_pc if func_high_pc is not None else 0xFFFF
                current_location = [(start, end, current_name, f'R{reg}')]
                continue

            # Frame base relative: DW_OP_fbreg <offset>
            m = re.search(r'DW_AT_location\s+\(DW_OP_fbreg ([+-]?\d+)\)', stripped)
            if m:
                offset = int(m.group(1))
                start = func_low_pc if func_low_pc is not None else 0
                end = func_high_pc if func_high_pc is not None else 0xFFFF
                current_location = [(start, end, current_name, f'stack{offset:+d}')]
                continue

            # Location list with PC ranges
            # [0x00000004, 0x00000020): DW_OP_reg3
            m = re.findall(
                r'\[(0x[0-9a-f]+),\s*(0x[0-9a-f]+)\):\s*(DW_OP_\w+(?:\s+[+-]?\d+)?)',
                stripped
            )
            if m:
                locs = []
                for start_s, end_s, op in m:
                    start = int(start_s, 16)
                    end = int(end_s, 16)
                    reg_m = re.match(r'DW_OP_reg(\d+)', op)
                    fbreg_m = re.match(r'DW_OP_fbreg\s+([+-]?\d+)', op)
                    if reg_m:
                        loc = f'R{int(reg_m.group(1))}'
                    elif fbreg_m:
                        offset = int(fbreg_m.group(1))
                        loc = f'stack{offset:+d}'
                    else:
                        loc = op
                    locs.append((start, end, current_name, loc))
                if locs:
                    current_location = locs

    # Flush last entry
    if current_tag == 'variable' and current_name and current_location:
        for loc in current_location:
            mappings.append(loc)

    return mappings


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <elf_file> [--dwarfdump PATH]", file=sys.stderr)
        sys.exit(1)

    elf_file = sys.argv[1]
    dwarfdump = 'llvm-dwarfdump'

    for i, arg in enumerate(sys.argv[2:], 2):
        if arg == '--dwarfdump' and i + 1 < len(sys.argv):
            dwarfdump = sys.argv[i + 1]

    # Run llvm-dwarfdump
    try:
        result = subprocess.run(
            [dwarfdump, '--debug-info', elf_file],
            capture_output=True, text=True, check=True
        )
    except FileNotFoundError:
        print(f"Error: {dwarfdump} not found. Install LLVM or use --dwarfdump PATH.",
              file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"Error running {dwarfdump}: {e.stderr}", file=sys.stderr)
        sys.exit(1)

    mappings = parse_dwarfdump(result.stdout)

    if not mappings:
        print("# No variable mappings found (compile with -g?)", file=sys.stderr)

    # Output varmap
    out_file = os.path.splitext(elf_file)[0] + '.varmap'
    with open(out_file, 'w') as f:
        f.write("# start_pc end_pc name location\n")
        for start, end, name, loc in mappings:
            # Convert byte addresses to word addresses (div by 2)
            f.write(f"0x{start // 2:04x} 0x{end // 2:04x} {name} {loc}\n")

    print(f"Wrote {len(mappings)} mappings to {out_file}", file=sys.stderr)


if __name__ == '__main__':
    main()
