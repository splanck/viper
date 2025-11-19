' Test 03: ANSI terminal control and colors
' Testing: CLS, COLOR, LOCATE

' Test basic ANSI colors
CLS
LOCATE 1, 1
Print "Testing ANSI Colors:"
Print

' Test different colors
COLOR 1, 0  ' Red on black
Print "Red text"

COLOR 2, 0  ' Green
Print "Green text"

COLOR 3, 0  ' Yellow
Print "Yellow text"

COLOR 4, 0  ' Blue
Print "Blue text"

COLOR 5, 0  ' Magenta
Print "Magenta text"

COLOR 6, 0  ' Cyan
Print "Cyan text"

COLOR 7, 0  ' White
Print "White text"

' Test LOCATE
COLOR 7, 0
LOCATE 15, 5
Print "Positioned at row 15, col 5"

LOCATE 17, 10
Print "Positioned at row 17, col 10"

' Test background colors
LOCATE 20, 1
COLOR 7, 1  ' White on red
Print "White on red background"

COLOR 0, 2  ' Black on green
Print "Black on green background"

' Reset
COLOR 7, 0
LOCATE 23, 1
Print "Test complete."
