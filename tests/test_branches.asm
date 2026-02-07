-- test_branches.asm
-- Tests: BZ.T taken/not-taken, forward/backward branches, loops, BHLEQ
--
-- NOTE: Simulator forwarding requires 1 NOP between write and read of
-- same register (no forwarding from decode_to_exec stage).
-- BZ flag check from exec_to_mem works correctly for instruction N-1.
--
-- Memory map for test results:
--   addr 0: BZ.T taken when Z=1 (expected: 0x0001)
--   addr 1: BZ.T fall-through when Z=0 (expected: 0x0001)
--   addr 2: Loop counter result (expected: 0x0005)
--   addr 3: Backward branch accumulate (expected: 0x0003)
--   addr 4: Forward branch over block (expected: 0x00AA)
--   addr 5: BHLEQ taken when HI==LO (expected: 0x0001)
--   addr 6: BHLEQ not-taken when HI!=LO (expected: 0x0001)

-- Setup constants
LIL $1, 1
LIL $2, 5
LIL $3, 3
NOP
NOP
NOP

-- ============================================================
-- Test 1: BZ.T taken (Z=1 -> branch)
-- SUB $14 produces 0 → Z=1 → BZ.T should branch
-- ============================================================
AND $4, $0          -- $4 = 0 (result register, default = fail)
AND $14, $0         -- $14 = 0, Z=1 (next instr reads Z from this)
BZ %T1_OK%,T        -- should branch (Z=1 from AND above)
NOP                  -- delay slot
J %T1_DONE%          -- only reached if NOT taken
NOP
T1_OK:
NOP                  -- gap: $4 cleared above, now OR reads it
OR $4, $1            -- $4 = 1 (branch was taken correctly)
T1_DONE:
NOP                  -- gap before SW reads $4
SW $4, 0($0)

-- ============================================================
-- Test 2: BZ.T fall-through (Z=0 -> don't branch)
-- OR produces non-zero → Z=0 → BZ.T should NOT branch
-- ============================================================
AND $5, $0          -- $5 = 0 (result, default = fail)
OR $14, $1          -- $14 = 1, Z=0
BZ %T2_BAD%,T       -- should NOT branch (Z=0)
NOP                  -- delay slot
NOP                  -- gap: $5 cleared, now OR reads it
OR $5, $1           -- $5 = 1 (fall-through is correct)
J %T2_DONE%
NOP
T2_BAD:
AND $5, $0          -- $5 = 0 (incorrectly taken)
T2_DONE:
NOP                  -- gap before SW
SW $5, 1($0)

-- ============================================================
-- Test 3: Loop — count from 0 to 5
-- ============================================================
AND $6, $0          -- $6 = 0 (counter)
LOOP:
  ADD $6, $1        -- counter++
  AND $14, $0       -- clear scratch
  NOP
  OR $14, $6        -- $14 = counter
  NOP
  SUB $14, $2       -- $14 = counter - 5, Z=1 when counter==5
  BZ %LOOP_END%,T   -- exit when counter reaches 5
  NOP               -- delay slot
  J %LOOP%
  NOP               -- delay slot
LOOP_END:
NOP
SW $6, 2($0)        -- store counter (expected: 5)

-- ============================================================
-- Test 4: Backward branch — accumulate 1+1+1 = 3
-- ============================================================
AND $7, $0          -- $7 = 0 (accumulator)
AND $8, $0          -- $8 = 0 (iteration counter)
LIL $9, 3
NOP
NOP
NOP
ACC_LOOP:
  ADD $7, $1        -- accumulator++
  ADD $8, $1        -- iteration++
  AND $14, $0
  NOP
  OR $14, $8
  NOP
  SUB $14, $9       -- Z=1 when iteration==3
  BZ %ACC_DONE%,T
  NOP
  J %ACC_LOOP%
  NOP
ACC_DONE:
NOP
SW $7, 3($0)        -- store accumulator (expected: 3)

-- ============================================================
-- Test 5: Forward branch — skip over a block of code
-- ============================================================
LIW $10, 0x00AA    -- $10 = 0xAA (correct value)
AND $14, $0         -- Z=1
BZ %SKIP_BLOCK%,T   -- branch forward over the bad write
NOP
LIW $10, 0x00FF    -- POISON: should be skipped
SKIP_BLOCK:
NOP
SW $10, 4($0)       -- store $10 (expected: 0xAA if skip worked)

-- ============================================================
-- Test 6: BHLEQ taken (HI == LO)
-- ============================================================
AND $11, $0         -- $11 = 0 (result, default = fail)
LHI 0x0042          -- HI = 0x0042
LLO 0x0042          -- LO = 0x0042 (HI == LO)
NOP
NOP
NOP
BHLEQ %T6_OK%       -- should branch (HI == LO)
NOP
J %T6_DONE%
NOP
T6_OK:
NOP
OR $11, $1          -- $11 = 1 (taken correctly)
T6_DONE:
NOP
SW $11, 5($0)

-- ============================================================
-- Test 7: BHLEQ not taken (HI != LO)
-- ============================================================
AND $12, $0         -- $12 = 0 (result, default = fail)
LHI 0x0099          -- HI = 0x0099
LLO 0x0042          -- LO = 0x0042 (HI != LO)
NOP
NOP
NOP
BHLEQ %T7_BAD%      -- should NOT branch (HI != LO)
NOP
NOP
OR $12, $1          -- $12 = 1 (fall-through is correct)
J %T7_DONE%
NOP
T7_BAD:
AND $12, $0
T7_DONE:
NOP
SW $12, 6($0)

-- End
HALT: J %HALT%
NOP
