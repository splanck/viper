' API Audit: Viper.Game.ParticleEmitter (BASIC)
PRINT "=== API Audit: Viper.Game.ParticleEmitter ==="
DIM pe AS OBJECT = Viper.Game.ParticleEmitter.New(100)
PRINT pe.Count
PRINT pe.IsEmitting
pe.SetPosition(100, 200)
pe.SetVelocity(-10, 10, -50, -10)
pe.SetLifetime(500, 2000)
pe.SetGravity(0, 98)
pe.SetSize(4, 1)
pe.Start()
PRINT pe.IsEmitting
pe.Stop()
PRINT pe.IsEmitting
PRINT pe.Rate
PRINT pe.X
PRINT pe.Y
PRINT pe.FadeOut
PRINT pe.Shrink
PRINT pe.Color
PRINT "=== ParticleEmitter Audit Complete ==="
END
