#!/usr/bin/env python3
"""Debug client for investigating test_sort failure via remote debug API.

Connects to the simulator's JSON-RPC debug server and single-steps through
the bubble sort, printing detailed state at every relevant instruction.
"""

import zmq
import json
import subprocess
import time
import sys
import os

# Key addresses from test_sort.ind  (all decimal = hex shown)
ADDRS = {
    0x22: "LW R6, 0(R5)",      # 34 INNER
    0x23: "LW R7, 1(R5)",      # 35
    0x27: "AND R8, R0",         # 39
    0x29: "OR R8, R6",          # 41
    0x2b: "SGT R8, R7",        # 43
    0x2c: "BZ NO_SWAP,T",      # 44
    0x2e: "SW R7, 0(R5)",      # 46
    0x2f: "SW R6, 1(R5)",      # 47
    0x30: "ADD R5, R1 (j++)",  # 48 NO_SWAP
    0x35: "SUB R14, R4",       # 53 inner loop check
    0x36: "BZ INNER_DONE,T",   # 54
    0x38: "J INNER",           # 56
    0x3a: "ADD R3, R1 (i++)",  # 58 INNER_DONE
    0x44: "SUB R14, R9",       # 68 outer loop check
    0x45: "BZ SORT_DONE,T",    # 69
    0x47: "J OUTER",           # 71
    0x49: "J HALT",            # 73 HALT
    # OUTER setup
    0x1a: "AND R4, R0",        # 26 OUTER
    0x1c: "OR R4, R2",         # 28
    0x1e: "SUB R4, R1",        # 30
    0x20: "SUB R4, R3",        # 32
    0x21: "AND R5, R0 (j=0)",  # 33
}

ADDR_INNER      = 0x22
ADDR_OR_R8      = 0x29
ADDR_SGT        = 0x2b
ADDR_BZ_SWAP    = 0x2c
ADDR_NO_SWAP    = 0x30
ADDR_SUB_LOOP   = 0x35
ADDR_BZ_INNER   = 0x36
ADDR_INNER_DONE = 0x3a
ADDR_HALT       = 0x49

PORT = 6808
SIM = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'builddir', 'sim')
PROG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_sort.bin')


class DebugClient:
    def __init__(self, port=PORT):
        self.ctx = zmq.Context()
        self.sock = self.ctx.socket(zmq.REQ)
        self.sock.connect(f"tcp://localhost:{port}")
        self.req_id = 0

    def call(self, method, params=None):
        self.req_id += 1
        msg = {"jsonrpc": "2.0", "method": method, "id": self.req_id}
        if params:
            msg["params"] = params
        self.sock.send_string(json.dumps(msg))
        resp = json.loads(self.sock.recv_string())
        if "error" in resp:
            raise RuntimeError(f"RPC error: {resp['error']}")
        return resp.get("result", resp)

    def step(self, count=1):
        return self.call("step", {"count": count})

    def registers(self):
        return self.call("registers")

    def memory(self, addr, count=1):
        return self.call("memory.read", {"address": addr, "count": count})

    def pipeline(self):
        return self.call("pipeline")

    def status(self):
        return self.call("status")

    def close(self):
        self.sock.close()
        self.ctx.term()


def hv(s):
    return int(s, 16) if isinstance(s, str) else int(s)


def get_regs(client):
    r = client.registers()
    return {
        'pc': hv(r['pc']),
        'gpr': [hv(v) for v in r['gpr']],
    }


def get_array(client, count=5):
    m = client.memory(0, count)
    return [hv(e["value"]) for e in m["data"]]


def get_pipeline_info(client):
    """Get pipeline stages and forwarding info."""
    p = client.pipeline()
    s = p["stages"]
    parts = []
    for name in ["IF", "ID", "EX", "MEM"]:
        st = s[name]
        bub = "[B]" if st["bubble"] else ""
        parts.append(f"{name}:{st['pc']}{bub} {st['asm']}")
    fwd = ""
    if s["ID"].get("fwd_alu_alu"):
        fwd += " fwd:ALU"
    if s["ID"].get("fwd_mem_alu"):
        fwd += " fwd:MEM"
    return " | ".join(parts) + fwd


def main():
    sim_path = os.path.abspath(SIM)
    prog_path = os.path.abspath(PROG)

    print(f"Starting simulator: {sim_path} -r {PORT} {prog_path}")
    sim_proc = subprocess.Popen(
        [sim_path, "-r", str(PORT), prog_path],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    time.sleep(0.5)

    if sim_proc.poll() is not None:
        print(f"Simulator exited early: {sim_proc.returncode}")
        print(sim_proc.stderr.read().decode())
        return 1

    try:
        c = DebugClient(PORT)
        print(f"Connected: {c.status()}\n")

        # Step through initialization
        for i in range(200):
            r = c.step()
            pc = hv(r["pc"])
            if pc == ADDR_INNER:
                break

        regs = get_regs(c)
        arr = get_array(c)
        print(f"Init done. Array: {arr}")
        print(f"  R1={regs['gpr'][1]} R2={regs['gpr'][2]} R3(i)={regs['gpr'][3]} "
              f"R4(lim)={regs['gpr'][4]} R5(j)={regs['gpr'][5]}\n")

        # Track pass/iteration
        pass_num = 0
        prev_j = -1
        halt_count = 0  # count consecutive cycles at HALT addr to distinguish transient from real

        print(f"{'CYC':>5} {'PC':>6} {'Instr':<24} {'R3(i)':>5} {'R4':>4} "
              f"{'R5(j)':>5} {'R6':>5} {'R7':>5} {'R8':>5} {'R14':>6} {'Extra'}")
        print("-" * 130)

        for step_num in range(8000):
            r = c.step()
            pc = hv(r["pc"])
            cycle = r.get("cycle", "?")

            # Detect real halt: PC stuck at HALT for several cycles
            if pc == ADDR_HALT:
                halt_count += 1
                if halt_count >= 10:
                    print(f"\n=== HALTED (PC stuck at 0x{ADDR_HALT:04x} for {halt_count} cycles) ===")
                    break
            else:
                halt_count = 0

            # Real halt from programEnd
            if r.get("halted"):
                print(f"\n=== programEnd() triggered at cycle {cycle} ===")
                break

            label = ADDRS.get(pc, "")
            if not label:
                continue  # skip non-interesting addresses

            regs = get_regs(c)
            g = regs['gpr']
            extra = ""

            if pc == ADDR_INNER:
                j = g[5]
                if j <= prev_j and prev_j >= 0:
                    pass_num += 1
                prev_j = j
                arr = get_array(c)
                extra = f"PASS {pass_num} arr={arr}"

            elif pc == ADDR_OR_R8:
                pipe = get_pipeline_info(c)
                extra = f"pipe: {pipe}"

            elif pc == ADDR_SGT:
                pipe = get_pipeline_info(c)
                extra = f"pipe: {pipe}"

            elif pc in (ADDR_BZ_SWAP, ADDR_BZ_INNER):
                arr = get_array(c)
                extra = f"arr={arr}"

            elif pc == ADDR_NO_SWAP:
                arr = get_array(c)
                extra = f"arr={arr}"

            elif pc == ADDR_INNER_DONE:
                arr = get_array(c)
                extra = f"=== INNER_DONE i→{g[3]+1} arr={arr} ==="

            elif pc == 0x1a:  # OUTER label
                arr = get_array(c)
                extra = f"OUTER start, arr={arr}"

            elif pc == ADDR_SUB_LOOP:
                pipe = get_pipeline_info(c)
                extra = f"pipe: {pipe}"

            print(f"{cycle:>5} 0x{pc:04x} {label:<24} "
                  f"{g[3]:>5} {g[4]:>4} {g[5]:>5} {g[6]:>5} {g[7]:>5} "
                  f"{g[8]:>5} {g[14]:>6} {extra}")

        # Final state
        arr = get_array(c)
        print(f"\nFinal array: {arr}")
        print(f"Expected:    [1, 2, 3, 4, 5]")
        print(f"Match: {arr == [1, 2, 3, 4, 5]}")

        c.close()
    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        sim_proc.terminate()
        sim_proc.wait(timeout=3)

    return 0


if __name__ == "__main__":
    sys.exit(main())
