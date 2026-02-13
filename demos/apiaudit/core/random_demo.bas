' random_demo.bas — Viper.Math.Random
PRINT "=== Viper.Math.Random Demo ==="
Viper.Math.Random.Seed(42)
PRINT Viper.Math.Random.Next()
PRINT Viper.Math.Random.NextInt(100)
PRINT Viper.Math.Random.Range(10, 20)
PRINT Viper.Math.Random.Dice(6)
PRINT Viper.Math.Random.Chance(0.5)
PRINT Viper.Math.Random.Gaussian(0.0, 1.0)
PRINT Viper.Math.Random.Exponential(1.0)
PRINT "done"
END
