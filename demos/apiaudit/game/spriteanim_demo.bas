' API Audit: Viper.Game.SpriteAnimation (BASIC)
PRINT "=== API Audit: Viper.Game.SpriteAnimation ==="
DIM sa AS OBJECT = Viper.Game.SpriteAnimation.New()
sa.Setup(0, 3, 100)
PRINT sa.FrameCount
PRINT sa.Frame
' NOTE: sa.Loop = 1 fails - "Loop" is a BASIC keyword (BUG A-071)
PRINT sa.FrameDuration
sa.Play()
PRINT sa.IsPlaying
PRINT sa.IsPaused
sa.Pause()
PRINT sa.IsPaused
sa.Resume()
PRINT sa.IsPlaying
sa.Stop()
PRINT sa.IsFinished
PRINT sa.Progress
PRINT "=== SpriteAnimation Audit Complete ==="
END
