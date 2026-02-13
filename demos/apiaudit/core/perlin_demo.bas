' perlin_demo.bas
PRINT "=== Viper.Math.PerlinNoise Demo ==="
DIM pn AS OBJECT
pn = NEW Viper.Math.PerlinNoise(42)
PRINT pn.Noise2D(1.0, 1.0)
PRINT pn.Noise3D(1.0, 1.0, 1.0)
PRINT pn.Octave2D(1.0, 1.0, 4, 0.5)
PRINT pn.Octave3D(1.0, 1.0, 1.0, 4, 0.5)
PRINT "done"
END
