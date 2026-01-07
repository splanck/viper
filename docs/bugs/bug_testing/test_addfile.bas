REM test_addfile.bas - Test ADDFILE keyword
ADDFILE "baseball_constants.bas"

REM Now we should have access to all constants
PRINT BOLD$; CYAN$; "╔══════════════════════╗"; RESET$
PRINT CYAN$; "║ ADDFILE TEST PASSED! ║"; RESET$
PRINT BOLD$; CYAN$; "╚══════════════════════╝"; RESET$
PRINT ""

PRINT "Testing color constants:"
PRINT RED$; "RED"; RESET$
PRINT GREEN$; "GREEN"; RESET$
PRINT YELLOW$; "YELLOW"; RESET$
PRINT CYAN$; "CYAN"; RESET$
PRINT BLUE$; "BLUE"; RESET$
PRINT MAGENTA$; "MAGENTA"; RESET$
PRINT ""

PRINT "Testing box drawing:"
PRINT BOX_TL$; BOX_H$; BOX_H$; BOX_H$; BOX_TR$
PRINT BOX_V$; " X "; BOX_V$
PRINT BOX_BL$; BOX_H$; BOX_H$; BOX_H$; BOX_BR$
PRINT ""

PRINT BOLD$; GREEN$; "✓ ADDFILE works!"; RESET$
