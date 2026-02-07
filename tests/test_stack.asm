-- test_stack.asm
-- Stack operations (PUSH/POP/SPRD/SPWR) and ADDI test program
-- Tests SP register, SP forwarding, back-to-back PUSH/POP, data hazards, ADDI
--
-- Memory map for test results:
--   addr 0:  SPRD initial SP (expected: 0xFFCF)
--   addr 1:  SP after PUSH (expected: 0xFFCE)
--   addr 2:  POP value (expected: 0x002A = 42)
--   addr 3:  SP after POP (expected: 0xFFCF)
--   addr 4:  SP after SPWR set to 100 (expected: 0x0064)
--   addr 5:  SP after SPWR restore (expected: 0xFFCF)
--   addr 6:  SP after 3 back-to-back PUSHes (expected: 0xFFCC)
--   addr 7:  Back-to-back POP 1 (expected: 0x001E = 30)
--   addr 8:  Back-to-back POP 2 (expected: 0x0014 = 20)
--   addr 9:  Back-to-back POP 3 (expected: 0x000A = 10)
--   addr 10: PUSH with ALU dependency (expected: 0x000C = 12)
--   addr 11: POP load-use (expected: 0x0063 = 99)
--   addr 12: ADDI positive (expected: 0x000F = 10+5)
--   addr 13: ADDI negative (expected: 0x000C = 15-3)
--   addr 14: ADDI forwarding chain (expected: 0x0019 = 12+7+6)

-- Setup: load test values
LIL $1, 42       -- R1 = 42
LIL $2, 10       -- R2 = 10
LIL $3, 20       -- R3 = 20
LIL $4, 30       -- R4 = 30

-- Test 1: SPRD initial SP = 0xFFCF
SPRD $5
SW $5, 0($0)

-- Test 2: PUSH $1 (42), then read SP. SP should be 0xFFCE
PUSH $1
SPRD $6
SW $6, 1($0)

-- Test 3: POP value = 42 (what was just pushed)
POP $7
ADD $7, $0       -- touch $7 to test POP load-use hazard (LW stall + WB fwd)
SW $7, 2($0)

-- Test 4: SP after POP = 0xFFCF (back to original)
SPRD $8
SW $8, 3($0)

-- Test 5: SPWR set SP=100 (0x0064)
LIL $9, 100
SPWR $9
SPRD $10
SW $10, 4($0)

-- Test 6: SPWR restore to 0xFFCF
LIW $9, 0xFFCF
SPWR $9
SPRD $10
SW $10, 5($0)

-- Test 7: 3 back-to-back PUSHes (SP forwarding test)
-- SP = 0xFFCF -> PUSH -> 0xFFCE -> PUSH -> 0xFFCD -> PUSH -> 0xFFCC
PUSH $2          -- push 10, SP = 0xFFCE
PUSH $3          -- push 20, SP = 0xFFCD
PUSH $4          -- push 30, SP = 0xFFCC
SPRD $11
SW $11, 6($0)

-- Test 8-10: 3 back-to-back POPs (SP forwarding test, LIFO order)
-- POP should get: 30 (last pushed), 20, 10
POP $12          -- pop 30
POP $13          -- pop 20
POP $14          -- pop 10
SW $12, 7($0)
SW $13, 8($0)
SW $14, 9($0)

-- Test 11: PUSH with ALU dependency
-- Clear registers fully (LIL only writes lower byte, stale upper bytes!)
-- R5 = 3+9 = 12, then PUSH immediately
-- This tests the SW stall mechanism (PUSH has mem_en=1, mem_w=1)
LIW $5, 3
LIW $6, 9
ADD $5, $6       -- R5 = 3+9 = 12
PUSH $5          -- PUSH should stall until R5 is available
POP $7           -- POP to verify the pushed value
ADD $7, $0       -- touch $7 for POP load-use
SW $7, 10($0)

-- Test 12: POP load-use (value used immediately after POP)
LIW $8, 99
PUSH $8
POP $9           -- POP 99 into R9
ADD $9, $0       -- immediate use of R9 (LW stall + WB forwarding)
SW $9, 11($0)

-- Test 13: ADDI positive (R2=10, ADDI +5 = 15)
AND $10, $0
OR $10, $2       -- R10 = 10
ADDI $10, 5      -- R10 = 10 + 5 = 15
SW $10, 12($0)

-- Test 14: ADDI negative (R10=15, ADDI -3 = 12)
ADDI $10, -3     -- R10 = 15 - 3 = 12
SW $10, 13($0)

-- Test 15: ADDI forwarding chain
-- R11 = 12 (from ADDI), then ADDI +7 = 19, then ADDI +6 = 25
AND $11, $0
OR $11, $10      -- R11 = 12
ADDI $11, 7      -- R11 = 12 + 7 = 19
ADDI $11, 6      -- R11 = 19 + 6 = 25
SW $11, 14($0)

-- End: infinite loop
HALT: J %HALT%
NOP
