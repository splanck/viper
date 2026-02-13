' =============================================================================
' API Audit: Viper.Math.PerlinNoise - Perlin Noise Generation (BASIC)
' =============================================================================
' Tests: New, Noise2D, Noise3D, Octave2D, Octave3D
' =============================================================================

PRINT "=== API Audit: Viper.Math.PerlinNoise ==="

' --- New ---
PRINT "--- New ---"
DIM p AS OBJECT
p = Viper.Math.PerlinNoise.New(42)
PRINT "PerlinNoise.New(42) created"

' --- Noise2D ---
PRINT "--- Noise2D ---"
PRINT "Noise2D(0.0, 0.0): "; Viper.Math.PerlinNoise.Noise2D(p, 0.0, 0.0)
PRINT "Noise2D(0.5, 0.5): "; Viper.Math.PerlinNoise.Noise2D(p, 0.5, 0.5)
PRINT "Noise2D(1.0, 1.0): "; Viper.Math.PerlinNoise.Noise2D(p, 1.0, 1.0)
PRINT "Noise2D(0.1, 0.2): "; Viper.Math.PerlinNoise.Noise2D(p, 0.1, 0.2)
PRINT "Noise2D(5.5, 3.7): "; Viper.Math.PerlinNoise.Noise2D(p, 5.5, 3.7)

' --- Noise3D ---
PRINT "--- Noise3D ---"
PRINT "Noise3D(0.0, 0.0, 0.0): "; Viper.Math.PerlinNoise.Noise3D(p, 0.0, 0.0, 0.0)
PRINT "Noise3D(0.5, 0.5, 0.5): "; Viper.Math.PerlinNoise.Noise3D(p, 0.5, 0.5, 0.5)
PRINT "Noise3D(1.0, 1.0, 1.0): "; Viper.Math.PerlinNoise.Noise3D(p, 1.0, 1.0, 1.0)
PRINT "Noise3D(0.1, 0.2, 0.3): "; Viper.Math.PerlinNoise.Noise3D(p, 0.1, 0.2, 0.3)

' --- Octave2D ---
PRINT "--- Octave2D ---"
PRINT "Octave2D(0.5, 0.5, 1, 0.5): "; Viper.Math.PerlinNoise.Octave2D(p, 0.5, 0.5, 1, 0.5)
PRINT "Octave2D(0.5, 0.5, 4, 0.5): "; Viper.Math.PerlinNoise.Octave2D(p, 0.5, 0.5, 4, 0.5)
PRINT "Octave2D(0.5, 0.5, 8, 0.5): "; Viper.Math.PerlinNoise.Octave2D(p, 0.5, 0.5, 8, 0.5)
PRINT "Octave2D(1.0, 2.0, 4, 0.7): "; Viper.Math.PerlinNoise.Octave2D(p, 1.0, 2.0, 4, 0.7)

' --- Octave3D ---
PRINT "--- Octave3D ---"
PRINT "Octave3D(0.5, 0.5, 0.5, 1, 0.5): "; Viper.Math.PerlinNoise.Octave3D(p, 0.5, 0.5, 0.5, 1, 0.5)
PRINT "Octave3D(0.5, 0.5, 0.5, 4, 0.5): "; Viper.Math.PerlinNoise.Octave3D(p, 0.5, 0.5, 0.5, 4, 0.5)
PRINT "Octave3D(0.5, 0.5, 0.5, 8, 0.5): "; Viper.Math.PerlinNoise.Octave3D(p, 0.5, 0.5, 0.5, 8, 0.5)

' --- Different seeds produce different results ---
PRINT "--- Different Seeds ---"
DIM p2 AS OBJECT
p2 = Viper.Math.PerlinNoise.New(99)
PRINT "Seed 42 Noise2D(0.5, 0.5): "; Viper.Math.PerlinNoise.Noise2D(p, 0.5, 0.5)
PRINT "Seed 99 Noise2D(0.5, 0.5): "; Viper.Math.PerlinNoise.Noise2D(p2, 0.5, 0.5)

PRINT "=== PerlinNoise Audit Complete ==="
END
