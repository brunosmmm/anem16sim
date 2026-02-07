-- test_memory.asm
-- Tests: SW/LW basic, offsets, load-use hazard, store-load roundtrip
--
-- 1 NOP required between write and read of same register.
--
-- Memory map for test results:
--   addr 0: Basic SW/LW roundtrip (expected: 0x0037)
--   addr 1: LW with offset verification (expected: 0x0037)
--   addr 2: Store with offset (expected: 0x0055)
--   addr 3: Load-use forwarding (expected: 0x006E = 0x37+0x37)
--   addr 4: Load-modify-store (expected: 0x003E = 0x37+7)
--   addr 5: Address register indexing (expected: 0x0012)
--   addr 6: Multiple store verification (expected: 0x00CC)

-- Setup constants
LIL $1, 7
LIW $2, 0x0037      -- test value 1 = 0x37 (55 decimal)
LIW $3, 0x0055      -- test value 2 = 0x55 (85 decimal)
NOP
NOP

-- ============================================================
-- Test 1: Basic SW then LW roundtrip
-- ============================================================
SW $2, 0($0)         -- mem[0] = 0x37
NOP
LW $5, 0($0)         -- $5 = mem[0] = 0x37
NOP
NOP
NOP
-- $5 should be 0x37, write it back to confirm
SW $5, 0($0)         -- mem[0] = $5 = 0x37

-- ============================================================
-- Test 2: LW verification — read back addr 0 into different register
-- ============================================================
LW $6, 0($0)         -- $6 = mem[0] = 0x37
NOP
NOP
NOP
SW $6, 1($0)         -- mem[1] = 0x37

-- ============================================================
-- Test 3: SW with offset
-- ============================================================
SW $3, 2($0)         -- mem[2] = 0x55

-- ============================================================
-- Test 4: Load-use forwarding test
-- LW followed by ALU use — tests stall+forward mechanism
-- ============================================================
SW $2, 8($0)         -- mem[8] = 0x37 (stage value)
NOP
LW $7, 8($0)         -- $7 = mem[8] = 0x37
NOP
NOP
-- Use $7 in computation
AND $8, $0           -- $8 = 0
NOP
ADD $8, $7           -- $8 = 0 + 0x37 = 0x37
NOP
ADD $8, $7           -- $8 = 0x37 + 0x37 = 0x6E
NOP
SW $8, 3($0)         -- mem[3] = 0x6E (110 decimal)

-- ============================================================
-- Test 5: Load-modify-store
-- Load a value, add to it, store at new address
-- ============================================================
LW $9, 0($0)         -- $9 = mem[0] = 0x37
NOP
NOP
NOP
ADD $9, $1           -- $9 = 0x37 + 7 = 0x3E
NOP
SW $9, 4($0)         -- mem[4] = 0x3E (62 decimal)

-- ============================================================
-- Test 6: Address register indexing
-- Use a register as base address for SW/LW
-- ============================================================
LIL $10, 5
NOP
NOP
NOP
LIW $11, 0x0012
SW $11, 0($10)       -- mem[5] = 0x12
NOP
LW $12, 0($10)       -- $12 = mem[5] = 0x12
NOP
NOP
NOP
SW $12, 5($0)        -- mem[5] = 0x12 (verify)

-- ============================================================
-- Test 7: Multiple stores to sequential addresses
-- ============================================================
LIW $13, 0x00CC
SW $13, 6($0)        -- mem[6] = 0xCC

-- End
HALT: J %HALT%
NOP
