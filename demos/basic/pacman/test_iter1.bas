' ============================================================================
' PACMAN Test Iteration 1 - Non-interactive version for testing
' ============================================================================

Dim frameCount As Integer
Dim maxFrames As Integer

frameCount = 0
maxFrames = 5

' Test Viper.Terminal.Clear
PRINT "Test 1: Viper.Terminal.Clear()"
Viper.Terminal.Clear()
PRINT "PASS: Clear executed"

' Test Viper.Terminal.SetColor
PRINT "Test 2: Viper.Terminal.SetColor(14, 0)"
Viper.Terminal.SetColor(14, 0)
PRINT "PASS: SetColor executed"

' Test Viper.Terminal.SetPosition
PRINT "Test 3: Viper.Terminal.SetPosition(5, 10)"
Viper.Terminal.SetPosition(5, 10)
PRINT "PASS: SetPosition executed"

' Test Viper.Time.SleepMs
PRINT "Test 4: Viper.Time.SleepMs(10)"
Viper.Time.SleepMs(10)
PRINT "PASS: SleepMs executed"

' Test simple loop with frame counting
PRINT "Test 5: Game loop with "
PRINT maxFrames
PRINT " frames"

Do While frameCount < maxFrames
    frameCount = frameCount + 1
    PRINT "Frame "
    PRINT frameCount
Loop

PRINT "PASS: Loop completed"

' Reset color
Viper.Terminal.SetColor(7, 0)
PRINT "All tests complete"
