-- test_sgt_bz.asm
-- Minimal test: SGT followed by BZ.T
-- Tests whether BZ correctly reads Z flag from SGT result
--
-- Memory map:
--   addr 0: SGT(3,4) + BZ taken (expected: 0x0001)
--   addr 1: SGT(5,2) + BZ not taken (expected: 0x0001)
--   addr 2: SGT result when NOT greater (expected: 0x0000)
--   addr 3: SGT result when greater (expected: 0x0001)

-- Setup
LIL $1, 3
LIL $2, 4
LIL $3, 5
LIL $10, 2
LIL $11, 1
NOP
NOP
NOP

-- ============================================================
-- Test 1: SGT(3,4) should produce 0, BZ should branch
-- ============================================================
AND $4, $0          -- R4 = 0 (default fail)
NOP
OR $4, $1           -- R4 = 3
NOP
SGT $4, $2          -- SGT(3, 4) → 0 (3 is NOT > 4)
NOP
NOP
NOP
SW $4, 2($0)        -- mem[2] = SGT result (expect 0)

-- Now test BZ with SGT
AND $5, $0          -- R5 = 0 (default fail)
NOP
OR $5, $1           -- R5 = 3
NOP
SGT $5, $2          -- SGT(3, 4) → 0, Z=1
BZ %T1_OK%,T        -- should branch (Z=1)
NOP                  -- delay slot
J %T1_FAIL%
NOP
T1_OK:
LIL $6, 1
NOP
NOP
NOP
SW $6, 0($0)        -- mem[0] = 1 (BZ taken correctly)
J %T2%
NOP
T1_FAIL:
LIL $6, 0
NOP
NOP
NOP
SW $6, 0($0)        -- mem[0] = 0 (BZ not taken — BUG)

-- ============================================================
-- Test 2: SGT(5,2) should produce 1, BZ should NOT branch
-- ============================================================
T2:
AND $7, $0          -- R7 = 0 (default fail)
NOP
OR $7, $3           -- R7 = 5
NOP
SGT $7, $10         -- SGT(5, 2) → 1 (5 IS > 2)
NOP
NOP
NOP
SW $7, 3($0)        -- mem[3] = SGT result (expect 1)

AND $8, $0          -- R8 = 0 (default fail)
NOP
OR $8, $3           -- R8 = 5
NOP
SGT $8, $10         -- SGT(5, 2) → 1, Z=0
BZ %T2_BAD%,T       -- should NOT branch (Z=0)
NOP                  -- delay slot
NOP
OR $8, $11          -- R8 = 1 (fall-through correct)
J %T2_DONE%
NOP
T2_BAD:
AND $8, $0          -- R8 = 0 (incorrectly taken)
T2_DONE:
NOP
SW $8, 1($0)        -- mem[1] = 1 if fall-through, 0 if taken (bug)

-- End
HALT: J %HALT%
NOP
