' bloomfilter_demo.bas - Comprehensive API audit for Viper.Collections.BloomFilter
' Tests: New, Add, MightContain, Count, Fpr, Clear, Merge

PRINT "=== BloomFilter API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM bf AS OBJECT
bf = Viper.Collections.BloomFilter.New(100, 0.01)
PRINT bf.Count       ' 0

' --- Add ---
PRINT "--- Add ---"
bf.Add("apple")
bf.Add("banana")
bf.Add("cherry")
PRINT bf.Count       ' 3

' --- MightContain ---
PRINT "--- MightContain ---"
PRINT bf.MightContain("apple")    ' 1
PRINT bf.MightContain("banana")   ' 1
PRINT bf.MightContain("cherry")   ' 1
PRINT bf.MightContain("grape")    ' 0 (probably)

' --- Fpr (estimated false positive rate) ---
PRINT "--- Fpr ---"
PRINT bf.Fpr()       ' very low

' --- Add more ---
PRINT "--- Add more ---"
bf.Add("date")
bf.Add("elderberry")
bf.Add("fig")
PRINT bf.Count       ' 6

' --- Merge ---
PRINT "--- Merge ---"
DIM bf2 AS OBJECT
bf2 = Viper.Collections.BloomFilter.New(100, 0.01)
bf2.Add("grape")
bf2.Add("honeydew")
bf2.Add("kiwi")
PRINT bf2.Count      ' 3

PRINT bf.Merge(bf2)  ' 1 (success)
PRINT bf.MightContain("grape")     ' 1
PRINT bf.MightContain("honeydew")  ' 1
PRINT bf.MightContain("kiwi")      ' 1

' --- Clear ---
PRINT "--- Clear ---"
bf.Clear()
PRINT bf.Count       ' 0
PRINT bf.MightContain("apple")    ' 0

PRINT "=== BloomFilter audit complete ==="
END
