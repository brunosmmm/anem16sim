-- test_sort.asm
-- Bubble sort: sort 5 numbers in ascending order (in-place)
--
-- Input array at addr 0-4:  [5, 3, 1, 4, 2]
-- Expected output at addr 0-4: [1, 2, 3, 4, 5]
--
-- Uses SGT for comparison. Tests: nested loops, memory ops,
-- conditional branches, address arithmetic.
--
-- Register allocation:
--   $1  = 1 (constant)
--   $2  = n = 5 (array size)
--   $3  = i (outer loop counter)
--   $4  = inner limit (n - 1 - i)
--   $5  = j (inner loop counter / memory base address)
--   $6  = arr[j]
--   $7  = arr[j+1]
--   $8  = comparison scratch
--   $9  = scratch for outer limit
--   $14 = scratch for loop checks

-- ============================================================
-- Initialize array in memory
-- ============================================================
LIW $6, 5
SW $6, 0($0)         -- mem[0] = 5
LIW $6, 3
SW $6, 1($0)         -- mem[1] = 3
LIW $6, 1
SW $6, 2($0)         -- mem[2] = 1
LIW $6, 4
SW $6, 3($0)         -- mem[3] = 4
LIW $6, 2
SW $6, 4($0)         -- mem[4] = 2

-- Setup constants
LIL $1, 1
LIL $2, 5
NOP
NOP
NOP

-- ============================================================
-- Bubble sort
-- ============================================================
AND $3, $0           -- i = 0

OUTER:
  -- Calculate inner limit: $4 = n - 1 - i
  AND $4, $0
  NOP
  OR $4, $2          -- $4 = n
  NOP
  SUB $4, $1         -- $4 = n - 1
  NOP
  SUB $4, $3         -- $4 = n - 1 - i

  -- Inner loop counter
  AND $5, $0         -- j = 0

  INNER:
    -- Load arr[j] and arr[j+1]
    LW $6, 0($5)    -- $6 = arr[j]
    LW $7, 1($5)    -- $7 = arr[j+1]
    NOP
    NOP
    NOP

    -- Compare: is arr[j] > arr[j+1]?
    AND $8, $0
    NOP
    OR $8, $6        -- $8 = arr[j]
    NOP
    SGT $8, $7       -- $8 = ($6 > $7) ? 1 : 0
    -- Z=1 when $8==0 (not greater), Z=0 when $8==1 (greater)
    BZ %NO_SWAP%,T   -- skip swap if NOT greater (Z=1)
    NOP              -- delay slot

    -- Swap arr[j] and arr[j+1]
    SW $7, 0($5)     -- arr[j] = old arr[j+1]
    SW $6, 1($5)     -- arr[j+1] = old arr[j]

    NO_SWAP:
    -- Increment inner counter
    ADD $5, $1       -- j++

    -- Check inner loop: j == inner_limit?
    AND $14, $0
    NOP
    OR $14, $5
    NOP
    SUB $14, $4      -- $14 = j - limit, Z=1 when j==limit
    BZ %INNER_DONE%,T
    NOP
    J %INNER%
    NOP

  INNER_DONE:
  -- Increment outer counter
  ADD $3, $1         -- i++

  -- Check outer loop: i == n - 1?
  AND $9, $0
  NOP
  OR $9, $2          -- $9 = n
  NOP
  SUB $9, $1         -- $9 = n - 1
  AND $14, $0
  NOP
  OR $14, $3
  NOP
  SUB $14, $9        -- $14 = i - (n-1), Z=1 when i==n-1
  BZ %SORT_DONE%,T
  NOP
  J %OUTER%
  NOP

SORT_DONE:

-- End: sorted array at mem[0..4] should be [1, 2, 3, 4, 5]
HALT: J %HALT%
NOP
