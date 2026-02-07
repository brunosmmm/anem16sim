-- test_subroutine.asm
-- Tests: JAL/JR calling convention, parameter passing, nested calls
--
-- Memory map for test results:
--   addr 0: DOUBLE(3) (expected: 0x0006)
--   addr 1: TRIPLE(5) (expected: 0x000F = 15)
--   addr 2: Nested: ADD_TEN then DOUBLE on input 3 (expected: 0x001A = 26)
--   addr 3: Return address verification (expected: 0x0001 = success)

-- Setup
LIL $1, 1
NOP
NOP
NOP

-- ============================================================
-- Test 1: Simple call — DOUBLE(3)
-- ============================================================
LIL $2, 3
NOP
NOP
NOP
JAL %DOUBLE%
NOP                  -- delay slot
NOP
SW $2, 0($0)         -- mem[0] = 6

-- ============================================================
-- Test 2: Simple call — TRIPLE(5)
-- ============================================================
LIL $2, 5
NOP
NOP
NOP
JAL %TRIPLE%
NOP                  -- delay slot
NOP
SW $2, 1($0)         -- mem[1] = 15

-- ============================================================
-- Test 3: Nested call — DOUBLE_PLUS_TEN(3)
-- Outer saves R15, calls ADD_TEN, doubles result, returns
-- Input: $2 = 3 -> ADD_TEN -> 13 -> DOUBLE in-place -> 26
-- ============================================================
LIL $2, 3
NOP
NOP
NOP
JAL %DOUBLE_PLUS_TEN%
NOP                  -- delay slot
NOP
SW $2, 2($0)         -- mem[2] = 26

-- ============================================================
-- Test 4: Verify return address works after all calls
-- ============================================================
AND $4, $0
NOP
OR $4, $1            -- $4 = 1 (success)
NOP
SW $4, 3($0)         -- mem[3] = 1

-- End
HALT: J %HALT%
NOP

-- ============================================================
-- Function: DOUBLE
-- Input: $2, Output: $2 = $2 * 2, Clobbers: $3
-- ============================================================
DOUBLE:
  AND $3, $0
  NOP
  OR $3, $2          -- $3 = $2
  NOP
  ADD $2, $3         -- $2 = $2 + $3 = 2 * original
  JR $15
  NOP                -- delay slot

-- ============================================================
-- Function: TRIPLE
-- Input: $2, Output: $2 = $2 * 3, Clobbers: $3
-- ============================================================
TRIPLE:
  AND $3, $0
  NOP
  OR $3, $2          -- $3 = $2 (save original)
  NOP
  ADD $2, $3         -- $2 = 2 * original
  NOP
  ADD $2, $3         -- $2 = 3 * original
  JR $15
  NOP                -- delay slot

-- ============================================================
-- Function: ADD_TEN
-- Input: $2, Output: $2 = $2 + 10, Clobbers: $4
-- ============================================================
ADD_TEN:
  LIL $4, 10
  NOP
  NOP
  NOP
  ADD $2, $4         -- $2 = $2 + 10
  JR $15
  NOP                -- delay slot

-- ============================================================
-- Function: DOUBLE_PLUS_TEN
-- Input: $2, Output: $2 = ($2 + 10) * 2, Clobbers: $3, $4, $13
-- ============================================================
DOUBLE_PLUS_TEN:
  -- Save return address
  AND $13, $0
  NOP
  OR $13, $15        -- $13 = return address

  -- Call ADD_TEN: $2 = $2 + 10
  JAL %ADD_TEN%
  NOP                -- delay slot

  -- Double: $2 = $2 * 2
  AND $3, $0
  NOP
  OR $3, $2          -- $3 = $2
  NOP
  ADD $2, $3         -- $2 = $2 + $3 = 2 * $2

  -- Restore return address
  AND $15, $0
  NOP
  OR $15, $13        -- R15 = saved return address
  NOP
  JR $15
  NOP                -- delay slot
