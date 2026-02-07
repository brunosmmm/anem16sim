-- test_hilo.asm
-- Tests: HI/LO register load, move, multiply, accumulate
--
-- Pipeline timing: HI/LO writes happen at WB (3 cycles after decode).
-- MFHI/MFLO captures hiout/loout at decode. Need 3 NOPs between
-- any HI/LO write and MFHI/MFLO read. Similarly for AIS chains.
--
-- Memory map for test results:
--   addr 0: LHI + MFHI (expected: 0x1234)
--   addr 1: LLO + MFLO (expected: 0x5678)
--   addr 2: MTHI from register + MFHI (expected: 0xAAAA)
--   addr 3: MTLO from register + MFLO (expected: 0xBBBB)
--   addr 4: MUL 6 * 7 (expected: 0x002A = 42)
--   addr 5: MUL 100 * 100 (expected: 0x2710 = 10000)
--   addr 6: AIS: HI + 25 (expected: 0x0019 = 25)
--   addr 7: AIS accumulate: HI = 10 + 20 + 30 (expected: 0x003C = 60)

-- ============================================================
-- Test 1: Load HI immediate, read back
-- LHI 0x1234 expands to LHH 0x12 + LHL 0x34
-- ============================================================
LHI 0x1234
NOP
NOP
NOP
MFHI $4
NOP
NOP
NOP
SW $4, 0($0)         -- mem[0] = 0x1234

-- ============================================================
-- Test 2: Load LO immediate, read back
-- LLO 0x5678 expands to LLH 0x56 + LLL 0x78
-- ============================================================
LLO 0x5678
NOP
NOP
NOP
MFLO $5
NOP
NOP
NOP
SW $5, 1($0)         -- mem[1] = 0x5678

-- ============================================================
-- Test 3: MTHI — move register to HI, read back
-- Need 3 NOPs between MTHI and MFHI for HI register to be written
-- ============================================================
LIW $6, 0xAAAA
NOP
NOP
NOP
MTHI $6
NOP
NOP
NOP
MFHI $7
NOP
NOP
NOP
SW $7, 2($0)         -- mem[2] = 0xAAAA

-- ============================================================
-- Test 4: MTLO — move register to LO, read back
-- ============================================================
LIW $8, 0xBBBB
NOP
NOP
NOP
MTLO $8
NOP
NOP
NOP
MFLO $9
NOP
NOP
NOP
SW $9, 3($0)         -- mem[3] = 0xBBBB

-- ============================================================
-- Test 5: MUL basic — 6 * 7 = 42
-- MUL $rd, $rs means Rd = Rd * Rs
-- ============================================================
LIL $10, 6
LIL $11, 7
NOP
NOP
NOP
MUL $10, $11         -- $10 = 6 * 7 = 42
NOP
NOP
NOP
SW $10, 4($0)        -- mem[4] = 42 = 0x2A

-- ============================================================
-- Test 6: MUL larger — 100 * 100 = 10000
-- Tests 16-bit multiply with larger values
-- ============================================================
LIL $10, 100
LIL $11, 100
NOP
NOP
NOP
MUL $10, $11         -- $10 = 100 * 100 = 10000
NOP
NOP
NOP
SW $10, 5($0)        -- mem[5] = 10000 = 0x2710

-- ============================================================
-- Test 7: AIS — Add Immediate Signed to {HI,LO}
-- AIS adds signed immediate to {HI,LO} as 32-bit number.
-- Clear both HI and LO first, then add 25.
-- Result: {HI,LO} = 25 → HI = 0x0000, LO = 0x0019
-- We check LO since the result fits in 16 bits.
-- Need 3 NOPs between each HI/LO write for WB timing.
-- ============================================================
LHI 0x0000           -- HI = 0
LLO 0x0000           -- LO = 0
NOP
NOP
NOP
AIS 25               -- {HI,LO} = 0 + 25 = 25
NOP
NOP
NOP
MFLO $12
NOP
NOP
NOP
SW $12, 6($0)        -- mem[6] = 25 = 0x19

-- ============================================================
-- Test 8: AIS accumulate — {HI,LO} = 10 + 20 + 30 = 60
-- Need 3 NOPs between each AIS for the HI/LO write to complete
-- ============================================================
LHI 0x0000           -- HI = 0
LLO 0x0000           -- LO = 0
NOP
NOP
NOP
AIS 10               -- {HI,LO} = 10
NOP
NOP
NOP
AIS 20               -- {HI,LO} = 30
NOP
NOP
NOP
AIS 30               -- {HI,LO} = 60
NOP
NOP
NOP
MFLO $13
NOP
NOP
NOP
SW $13, 7($0)        -- mem[7] = 60 = 0x3C

-- End
HALT: J %HALT%
NOP
