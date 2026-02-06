# anem16sim

Cycle-accurate simulator for the ANEM16 pipelined 16-bit CPU. Models the 5-stage pipeline (IF, ID, EX, MEM, WB) with data forwarding, hazard detection, and branch delay slots, matching the `anem16pipe` VHDL hardware implementation.

## Building

Requires C++20, Meson, and the following dependencies (fetched automatically via Meson wraps):

- [nlohmann/json](https://github.com/nlohmann/json) (header-only)
- [libzmq](https://zeromq.org/) + [cppzmq](https://github.com/zeromq/cppzmq)

```
meson setup builddir
meson compile -C builddir
```

The binary is at `builddir/sim`.

## Usage

```
sim [options] <program>
```

### Options

| Flag | Description |
|------|-------------|
| `-d` | Interactive debug mode (REPL) |
| `-r [port]` | Remote debug server (JSON-RPC over ZMQ, default port 6808) |
| `-t` | Print execution trace to stdout |
| `-T <file>` | Write HW-compatible trace to file (for diffing against VHDL traces) |
| `-o <file>` | Save JSON state snapshot on program completion |
| `-s` | Print statistics on exit |
| `-m N` | Set max cycle count (default 10000) |
| `-h` | Show help |

### Program file formats

The loader auto-detects the format from the first line:

1. **Intel HEX** (`.hex`): Standard Intel HEX format
2. **Address + binary** (`.contents.txt`): Space-separated binary address and instruction, one per line
3. **Plain binary** (`.bin`): One 16-bit instruction per line in binary, address = line number

### Examples

```bash
# Batch run, show registers and first 16 memory words on exit
sim tests/test_sort.bin

# Batch run with statistics
sim -s tests/test_fibonacci.bin

# Batch run with execution trace
sim -t -m 50000 tests/test_sort.bin

# Interactive debugging
sim -d tests/test_sort.bin

# Generate HW-compatible trace and JSON snapshot
sim -T output.trace -o output.json tests/test_sort.bin

# Remote debug server on port 7000
sim -r 7000 tests/test_sort.bin
```

## Interactive Debugger

Start with `sim -d <program>`. The debugger provides a REPL with the following commands:

### Execution control

| Command | Description |
|---------|-------------|
| `s [N]` | Step N clock cycles (default 1) |
| `c` | Continue until breakpoint, watchpoint, or halt |
| `reset` | Reset CPU (PC = 0, clear registers and memory) |
| `q` | Quit |

### Breakpoints and watchpoints

| Command | Description |
|---------|-------------|
| `b` | List all breakpoints |
| `b <addr>` | Set breakpoint at address |
| `db <addr>` | Delete breakpoint |
| `w` | List all watchpoints |
| `w <addr>` | Set data memory watchpoint at address |
| `dw <addr>` | Delete watchpoint |

Breakpoints halt execution when the PC reaches the address. Watchpoints halt execution when data memory at the address is read or written.

### Inspection

| Command | Description |
|---------|-------------|
| `r` | Show all registers (PC, R0-R15, HI, LO) |
| `r N` | Show register N (0-15) |
| `m <start> [N]` | Dump N data memory words starting at address (default 16) |
| `d <start> [N]` | Disassemble N instructions starting at address (default 16) |
| `p` | Show pipeline state (all 4 visible stages, forwarding, stall) |
| `t [on\|off]` | Toggle or set execution trace |
| `stats` | Show simulation statistics |

### State save/load

| Command | Description |
|---------|-------------|
| `save <file>` | Save full CPU state snapshot to JSON file |
| `load <file>` | Load CPU state snapshot from JSON file |

Snapshots include all registers, pipeline registers, data memory (sparse, non-zero only), counters, and internal state. Instruction memory is not saved; reload the program file before loading a snapshot.

Addresses can be decimal or hex with `0x` prefix (e.g., `b 0x0020` or `b 32`).

### Example session

```
$ sim -d tests/test_sort.bin
ANEM16 Debugger. Type 'h' for help.
Program loaded, PC=0x0000
anem> d 0 10
    [0x0000] LIL R1, 0x03
    [0x0001] NOP
    [0x0002] SW R1, 0(R0)
    ...
anem> b 0x0020
Breakpoint set at 0x0020
anem> c
Breakpoint hit at PC=0x0020
anem> r
PC  = 0x0020
R0  = 0x0000  R1  = 0x0001  R2  = 0x0005  R3  = 0x0004
...
anem> p
=== Pipeline (cycle 42) ===
  IF:  [0x0020] AND R5, R0
  ID:  [0x001f] J 0x001e
  EX:  [0x0020] AND R5, R0
  MEM: [0x001f] NOP
  Stall: none
anem> m 0 5
  [0x0000] = 0x0003
  [0x0001] = 0x0001
  [0x0002] = 0x0005
  [0x0003] = 0x0004
  [0x0004] = 0x0002
anem> save state.json
Snapshot saved to state.json
anem> q
```

## Remote Debug Server (JSON-RPC over ZMQ)

Start with `sim -r [port] <program>`. The server binds two ZMQ sockets:

- **REP** on `tcp://*:<port>` (default 6808) — for request/response
- **PUB** on `tcp://*:<port+1>` (default 6809) — for async notifications

Clients connect with ZMQ REQ + SUB sockets. All messages are JSON-RPC 2.0.

### Methods

All addresses and values are hex strings (e.g., `"0x0042"`). Addresses also accept integers.

#### Execution control

| Method | Params | Result |
|--------|--------|--------|
| `step` | `{"count": N}` | `{"pc", "cycle", "halted", "stopped_reason", "watch_access"?}` |
| `continue` | `{}` | `{"status": "running"}` (immediate; `stopped` notification follows on PUB) |
| `pause` | `{}` | `{"pc", "cycle"}` |
| `reset` | `{}` | `{"pc": "0x0000"}` |
| `status` | `{}` | `{"running", "halted", "pc", "cycle"}` |

#### Breakpoints and watchpoints

| Method | Params | Result |
|--------|--------|--------|
| `breakpoint.add` | `{"address": "0x0010"}` | `{"address": "0x0010"}` |
| `breakpoint.remove` | `{"address": "0x0010"}` | `{"removed": true}` |
| `breakpoint.list` | `{}` | `{"breakpoints": ["0x0010", ...]}` |
| `watchpoint.add` | `{"address": "0x0100"}` | `{"address": "0x0100"}` |
| `watchpoint.remove` | `{"address": "0x0100"}` | `{"removed": true}` |
| `watchpoint.list` | `{}` | `{"watchpoints": ["0x0100", ...]}` |

#### Inspection

| Method | Params | Result |
|--------|--------|--------|
| `registers` | `{}` | `{"pc", "gpr": [...], "hi", "lo"}` |
| `registers` | `{"register": N}` | `{"register": N, "value": "0x..."}` |
| `memory.read` | `{"address": "0x0000", "count": 16}` | `{"data": [{"addr", "value"}, ...]}` |
| `memory.write` | `{"address": "0x0000", "value": "0x1234"}` | `{"address", "value"}` |
| `disassemble` | `{"address": "0x0000", "count": 16}` | `{"instructions": [{"addr", "asm", "word", "current"}, ...]}` |
| `pipeline` | `{}` | `{"cycle", "stages": {"IF", "ID", "EX", "MEM"}, "stalled"}` |
| `trace` | `{"enabled": true}` | `{"enabled": true}` |
| `stats` | `{}` | `{"cycles", "instructions", "stalls", "bubbles", "fwd_alu_alu", "fwd_mem_alu"}` |

#### Snapshots

| Method | Params | Result |
|--------|--------|--------|
| `snapshot.save` | `{"path": "/tmp/state.json"}` | `{"path": "/tmp/state.json"}` |
| `snapshot.load` | `{"path": "/tmp/state.json"}` | `{"path", "pc"}` |

### Notifications (PUB socket)

Published as JSON-RPC notifications (no `id` field):

```json
{"jsonrpc": "2.0", "method": "stopped", "params": {"reason": "breakpoint", "pc": "0x0010", "cycle": 456}}
{"jsonrpc": "2.0", "method": "stopped", "params": {"reason": "watchpoint", "pc": "0x0011", "cycle": 457, "watch_access": {"address": "0x0100", "value": "0x00ff", "write": true}}}
{"jsonrpc": "2.0", "method": "stopped", "params": {"reason": "halted", "pc": "0x0042", "cycle": 500}}
{"jsonrpc": "2.0", "method": "stopped", "params": {"reason": "paused", "pc": "0x0030", "cycle": 300}}
{"jsonrpc": "2.0", "method": "trace", "params": {"cycle": 123, "pc": "0x0042", "asm": "ADD R1, R2"}}
```

### Error codes

| Code | Meaning |
|------|---------|
| -32700 | Parse error (invalid JSON) |
| -32600 | Invalid request |
| -32601 | Method not found |
| -32602 | Invalid params |
| -1 | Simulator error |
| -2 | Simulator is running (only `pause`, `status`, and breakpoint/watchpoint methods allowed) |
| -3 | Simulator is not running (cannot pause) |

### Python client example

```python
import zmq, json

ctx = zmq.Context()

# REQ socket for commands
req = ctx.socket(zmq.REQ)
req.connect("tcp://localhost:6808")

# SUB socket for notifications
sub = ctx.socket(zmq.SUB)
sub.connect("tcp://localhost:6809")
sub.setsockopt_string(zmq.SUBSCRIBE, "")

def call(method, params=None):
    msg = {"jsonrpc": "2.0", "method": method, "id": 1}
    if params:
        msg["params"] = params
    req.send_json(msg)
    return req.recv_json()

# Step 10 cycles
print(call("step", {"count": 10}))

# Set breakpoint and continue
call("breakpoint.add", {"address": "0x0020"})
call("continue")

# Wait for stopped notification
note = json.loads(sub.recv_string())
print(f"Stopped: {note['params']['reason']} at {note['params']['pc']}")

# Inspect state
print(call("registers"))
print(call("memory.read", {"address": "0x0000", "count": 8}))
print(call("pipeline"))
```

## HW-Compatible Trace Output

The `-T <file>` flag generates a trace file in the same format as the `anem16pipe` VHDL testbench, allowing direct diff-based comparison between simulator and hardware.

### Format

```
# anem16-trace v1
MW <addr> <data>
MW <addr> <data>
...
RF <idx> <value>
...
SR <name> <value>
SR <name> <value>
END <mw_count>
```

- **MW**: Memory write event (4-digit hex, lowercase, zero-padded). Emitted for every `SW` instruction commit. MAC peripheral writes (addr >= 0xFFD0) are excluded.
- **RF**: Register file dump at program end. Index is decimal (0-15), value is 4-digit hex.
- **SR**: Special registers (HI, LO) at program end.
- **END**: Final line with total MW count (decimal).

### Comparing traces

```bash
# Generate simulator trace
sim -T sim.trace tests/test_sort.bin

# Diff against hardware trace
diff sim.trace hw.trace
```

## Architecture

### CPU pipeline

```
IF  →  ID  →  EX  →  MEM  →  WB
         ↑         ↓
         └── forwarding ──┘
```

- **5-stage pipeline**: Instruction Fetch, Instruction Decode, Execute, Memory, Writeback
- **Data forwarding**: EX→ID (ALU→ALU) and MEM→ID (MEM→ALU) forwarding paths
- **Hazard detection**: 0-gap RAW hazards automatically stall for 1 cycle; LW-use hazards stall for 1 additional cycle
- **Branch delay slot**: All branches and jumps (J, JAL, JR, BZ, BHLEQ) execute the next instruction before taking effect

### Registers

- **R0-R15**: 16 general-purpose 16-bit registers (R0 hardwired to 0, R15 is link register for JAL)
- **HI, LO**: Special registers for MAC/multiply operations
- **PC**: Program counter

### Memory

- **Instruction memory**: 128K words
- **Data memory**: 64K words (including memory-mapped peripherals)
- **MAC peripheral**: Memory-mapped multiply-accumulate unit at address 0xFFD0

### Instruction set

| Opcode | Mnemonic | Description |
|--------|----------|-------------|
| 0x0 | R-type | Register ALU ops: ADD, SUB, AND, OR, XOR, NOR, SLT, SGT, MUL |
| 0x1 | S-type | Shift ops: SHL, SHR, SAR, ROL, ROR |
| 0x2 | SW | Store word to data memory |
| 0x3 | LW | Load word from data memory |
| 0x4 | LIU | Load immediate upper byte |
| 0x5 | LIL | Load immediate lower byte |
| 0x6 | BHLEQ | Branch if HI == LO |
| 0x8-A | BZ | Branch if zero |
| 0xC | JR | Jump register |
| 0xD | JAL | Jump and link (saves return address to R15) |
| 0xE | M1 | Special: MFHI, MFLO, MTHI, MTLO, LHL, LHH, LLL, LLH, AIS, AIH, AIL |
| 0xF | J | Unconditional jump |
