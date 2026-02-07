-- test_fibonacci.asm
-- Compute first 10 Fibonacci numbers, store to data memory
--
-- Algorithm: F(0)=0, F(1)=1, F(n)=F(n-1)+F(n-2)
--
-- Memory map (expected values):
--   addr 0: F(0)  = 0x0000
--   addr 1: F(1)  = 0x0001
--   addr 2: F(2)  = 0x0001
--   addr 3: F(3)  = 0x0002
--   addr 4: F(4)  = 0x0003
--   addr 5: F(5)  = 0x0005
--   addr 6: F(6)  = 0x0008
--   addr 7: F(7)  = 0x000D (13)
--   addr 8: F(8)  = 0x0015 (21)
--   addr 9: F(9)  = 0x0022 (34)

-- Register allocation:
--   $1  = 1 (constant for increment)
--   $2  = F(n-2) (previous-previous)
--   $3  = F(n-1) (previous)
--   $4  = address counter
--   $5  = limit (10)
--   $6  = F(n) (current, computed)
--   $14 = scratch for comparison

-- Setup
LIL $1, 1
LIL $2, 0            -- F(n-2) = 0
LIL $3, 1            -- F(n-1) = 1
LIL $4, 2            -- start storing at addr 2
LIL $5, 10           -- compute 10 numbers total
NOP
NOP
NOP

-- Store base cases
SW $2, 0($0)         -- mem[0] = F(0) = 0
SW $3, 1($0)         -- mem[1] = F(1) = 1

-- Loop: compute F(n) = F(n-1) + F(n-2)
FIB_LOOP:
  -- Compute: $6 = $2 + $3
  AND $6, $0         -- clear $6
  NOP
  ADD $6, $2         -- $6 = F(n-2)
  NOP
  ADD $6, $3         -- $6 = F(n-2) + F(n-1) = F(n)

  -- Store to memory at current address
  NOP
  SW $6, 0($4)       -- mem[addr] = F(n)

  -- Shift: F(n-2) = F(n-1), F(n-1) = F(n)
  AND $2, $0         -- clear $2
  NOP
  OR $2, $3          -- $2 = old F(n-1)
  AND $3, $0         -- clear $3
  NOP
  OR $3, $6          -- $3 = F(n)

  -- Increment address
  ADD $4, $1         -- addr++

  -- Check termination: addr == limit?
  AND $14, $0
  NOP
  OR $14, $4
  NOP
  SUB $14, $5        -- $14 = addr - 10, Z=1 when addr==10
  BZ %FIB_DONE%,T
  NOP                -- delay slot
  J %FIB_LOOP%
  NOP                -- delay slot

FIB_DONE:

-- End
HALT: J %HALT%
NOP
