' screenfx_demo.bas
PRINT "=== Viper.Game.ScreenFX Demo ==="
DIM fx AS OBJECT
fx = NEW Viper.Game.ScreenFX()
PRINT fx.IsActive
PRINT fx.ShakeX
PRINT fx.ShakeY
PRINT fx.OverlayColor
PRINT fx.OverlayAlpha
fx.CancelAll()
PRINT "done"
END
