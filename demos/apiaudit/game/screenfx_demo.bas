' screenfx_demo.bas - Comprehensive API audit for Viper.Game.ScreenFX
' Tests: New, Shake, Flash, FadeIn, FadeOut, Update, IsActive, IsTypeActive,
'        ShakeX, ShakeY, OverlayColor, OverlayAlpha, CancelAll, CancelType
' Note: ScreenFX is designed for display effects but tested headlessly here.

PRINT "=== ScreenFX API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM fx AS OBJECT
fx = Viper.Game.ScreenFX.New()
PRINT fx.IsActive          ' 0
PRINT fx.ShakeX            ' 0
PRINT fx.ShakeY            ' 0
PRINT fx.OverlayColor      ' 0
PRINT fx.OverlayAlpha      ' 0

' --- Shake (intensity, duration, decay) ---
PRINT "--- Shake ---"
fx.Shake(10, 500, 5)
PRINT fx.IsActive          ' 1
PRINT fx.IsTypeActive(0)   ' 1

' --- Update (deltaMs) ---
PRINT "--- Update ---"
fx.Update(16)
PRINT fx.IsActive          ' 1
PRINT fx.ShakeX
PRINT fx.ShakeY

' --- CancelType ---
PRINT "--- CancelType ---"
fx.CancelType(0)
fx.Update(16)
PRINT fx.ShakeX            ' 0
PRINT fx.ShakeY            ' 0

' --- Flash (color, duration) ---
PRINT "--- Flash ---"
fx.Flash(16777215, 300)
PRINT fx.IsActive          ' 1
fx.Update(16)
PRINT fx.OverlayColor
PRINT fx.OverlayAlpha

' --- FadeIn (color, duration) ---
PRINT "--- FadeIn ---"
fx.CancelAll()
fx.FadeIn(0, 500)
PRINT fx.IsActive          ' 1
fx.Update(100)
PRINT fx.OverlayAlpha

' --- FadeOut (color, duration) ---
PRINT "--- FadeOut ---"
fx.CancelAll()
fx.FadeOut(0, 500)
PRINT fx.IsActive          ' 1
fx.Update(100)
PRINT fx.OverlayAlpha

' --- CancelAll ---
PRINT "--- CancelAll ---"
fx.Shake(20, 1000, 10)
fx.Flash(255, 1000)
PRINT fx.IsActive          ' 1
fx.CancelAll()
fx.Update(16)
PRINT fx.IsActive          ' 0
PRINT fx.ShakeX            ' 0
PRINT fx.ShakeY            ' 0

' --- Multiple effects simultaneously ---
PRINT "--- Multiple effects ---"
fx.Shake(5, 200, 2)
fx.Flash(16711680, 200)
PRINT fx.IsActive          ' 1
fx.Update(16)
fx.Update(16)
fx.Update(16)

' --- IsTypeActive ---
PRINT "--- IsTypeActive ---"
fx.CancelAll()
PRINT fx.IsTypeActive(0)   ' 0
fx.Shake(10, 100, 5)
PRINT fx.IsTypeActive(0)   ' 1

' --- Cleanup ---
fx.CancelAll()
PRINT fx.IsActive          ' 0

PRINT "=== ScreenFX audit complete ==="
END
