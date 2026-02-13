' bits_demo.bas — Viper.Math.Bits
PRINT "=== Viper.Math.Bits Demo ==="
PRINT Viper.Math.Bits.And(12, 10)
PRINT Viper.Math.Bits.Or(12, 10)
PRINT Viper.Math.Bits.Xor(12, 10)
PRINT Viper.Math.Bits.Not(0)
PRINT Viper.Math.Bits.Flip(255)
PRINT Viper.Math.Bits.Shl(1, 4)
PRINT Viper.Math.Bits.Shr(-16, 2)
PRINT Viper.Math.Bits.Ushr(-16, 2)
PRINT Viper.Math.Bits.Count(255)
PRINT Viper.Math.Bits.LeadZ(1)
PRINT Viper.Math.Bits.TrailZ(16)
PRINT Viper.Math.Bits.Swap(1)
PRINT Viper.Math.Bits.Set(0, 3)
PRINT Viper.Math.Bits.Clear(15, 2)
PRINT Viper.Math.Bits.Get(8, 3)
PRINT Viper.Math.Bits.Toggle(8, 3)
PRINT Viper.Math.Bits.Rotl(1, 1)
PRINT Viper.Math.Bits.Rotr(1, 1)
PRINT "done"
END
