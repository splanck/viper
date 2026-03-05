' =============================================================================
' API Audit: Viper.Math.Random - Random Number Generation (BASIC)
' =============================================================================
' Tests: Seed, Next, NextInt, Range, Dice, Chance, Gaussian, Exponential
' Note: Random outputs are non-deterministic, so we just verify no crashes.
' =============================================================================

PRINT "=== API Audit: Viper.Math.Random ==="

' --- Seed ---
PRINT "--- Seed ---"
Viper.Math.Random.Seed(42)
PRINT "Seeded with 42"

' --- Next ---
PRINT "--- Next ---"
PRINT "Random.Next() [0.0..1.0]: "; Viper.Math.Random.Next()
PRINT "Random.Next() [0.0..1.0]: "; Viper.Math.Random.Next()
PRINT "Random.Next() [0.0..1.0]: "; Viper.Math.Random.Next()

' --- NextInt ---
PRINT "--- NextInt ---"
PRINT "Random.NextInt(100): "; Viper.Math.Random.NextInt(100)
PRINT "Random.NextInt(100): "; Viper.Math.Random.NextInt(100)
PRINT "Random.NextInt(10): "; Viper.Math.Random.NextInt(10)
PRINT "Random.NextInt(1): "; Viper.Math.Random.NextInt(1)

' --- Range ---
PRINT "--- Range ---"
PRINT "Random.Range(1, 6): "; Viper.Math.Random.Range(1, 6)
PRINT "Random.Range(1, 6): "; Viper.Math.Random.Range(1, 6)
PRINT "Random.Range(10, 20): "; Viper.Math.Random.Range(10, 20)
PRINT "Random.Range(-5, 5): "; Viper.Math.Random.Range(-5, 5)

' --- Dice ---
PRINT "--- Dice ---"
PRINT "Random.Dice(6): "; Viper.Math.Random.Dice(6)
PRINT "Random.Dice(6): "; Viper.Math.Random.Dice(6)
PRINT "Random.Dice(20): "; Viper.Math.Random.Dice(20)

' --- Chance ---
PRINT "--- Chance ---"
PRINT "Random.Chance(1.0) [always true]: "; Viper.Math.Random.Chance(1.0)
PRINT "Random.Chance(0.0) [always false]: "; Viper.Math.Random.Chance(0.0)
PRINT "Random.Chance(0.5): "; Viper.Math.Random.Chance(0.5)

' --- Gaussian ---
PRINT "--- Gaussian ---"
PRINT "Random.Gaussian(0.0, 1.0): "; Viper.Math.Random.Gaussian(0.0, 1.0)
PRINT "Random.Gaussian(0.0, 1.0): "; Viper.Math.Random.Gaussian(0.0, 1.0)
PRINT "Random.Gaussian(100.0, 15.0): "; Viper.Math.Random.Gaussian(100.0, 15.0)

' --- Exponential ---
PRINT "--- Exponential ---"
PRINT "Random.Exponential(1.0): "; Viper.Math.Random.Exponential(1.0)
PRINT "Random.Exponential(1.0): "; Viper.Math.Random.Exponential(1.0)
PRINT "Random.Exponential(0.5): "; Viper.Math.Random.Exponential(0.5)

PRINT "=== Random Audit Complete ==="
END
