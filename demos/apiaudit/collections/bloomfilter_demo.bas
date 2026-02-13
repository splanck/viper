' bloomfilter_demo.bas
PRINT "=== Viper.Collections.BloomFilter Demo ==="
DIM bf AS OBJECT
bf = NEW Viper.Collections.BloomFilter(1000, 0.01)
bf.Add("apple")
bf.Add("banana")
bf.Add("cherry")
PRINT bf.Count
PRINT bf.MightContain("apple")
PRINT bf.MightContain("grape")
bf.Clear()
PRINT bf.Count
PRINT "done"
END
