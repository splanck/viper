' Test graphics infrastructure wiring
' Note: Full BASIC graphics integration pending frontend runtime lookup

' Test manual RGB calculation (same algorithm as rt_gfx_rgb)
DIM red AS INTEGER
DIM green AS INTEGER
DIM blue AS INTEGER
DIM white AS INTEGER

' RGB encoding: 0x00RRGGBB = R*65536 + G*256 + B
red = 255 * 65536
green = 255 * 256
blue = 255
white = 255 * 65536 + 255 * 256 + 255

IF red = 16711680 THEN
    PRINT "PASS: red color"
ELSE
    PRINT "FAIL: red"
END IF

IF green = 65280 THEN
    PRINT "PASS: green color"
ELSE
    PRINT "FAIL: green"
END IF

IF blue = 255 THEN
    PRINT "PASS: blue color"
ELSE
    PRINT "FAIL: blue"
END IF

IF white = 16777215 THEN
    PRINT "PASS: white color"
ELSE
    PRINT "FAIL: white"
END IF

PRINT "=== GRAPHICS BASIC TESTS COMPLETE ==="
