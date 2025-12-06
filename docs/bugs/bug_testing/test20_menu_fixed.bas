' Test 20: Test fixed main menu display

CLS

' Title banner
LOCATE 2, 1
COLOR 14, 0
Print "          ╔════════════════════════════╗"
LOCATE 3, 1
Print "          ║                            ║"
LOCATE 4, 1
Print "          ║  ";
COLOR 11, 0
Print "  ██    ██ █████ █████";
COLOR 14, 0
Print "  ║"
LOCATE 5, 1
Print "          ║  ";
COLOR 11, 0
Print "  ██    ██   ██  ██  █";
COLOR 14, 0
Print "  ║"
LOCATE 6, 1
Print "          ║  ";
COLOR 11, 0
Print "  ██    ██   ██  █████";
COLOR 14, 0
Print "  ║"
LOCATE 7, 1
Print "          ║  ";
COLOR 11, 0
Print "   ██  ██    ██  ██  █";
COLOR 14, 0
Print "  ║"
LOCATE 8, 1
Print "          ║  ";
COLOR 11, 0
Print "    ████     ██  █████";
COLOR 14, 0
Print "  ║"
LOCATE 9, 1
Print "          ║                            ║"
LOCATE 10, 1
Print "          ║    ";
COLOR 10, 0
Print "Viper BASIC Demo";
COLOR 14, 0
Print "    ║"
LOCATE 11, 1
Print "          ╚════════════════════════════╝"

' Menu options
LOCATE 14, 1
COLOR 15, 0
Print "               [1] ";
COLOR 11, 0
Print "NEW GAME"

LOCATE 16, 1
COLOR 15, 0
Print "               [2] ";
COLOR 10, 0
Print "HIGH SCORES"

LOCATE 18, 1
COLOR 15, 0
Print "               [3] ";
COLOR 9, 0
Print "INSTRUCTIONS"

LOCATE 20, 1
COLOR 15, 0
Print "               [Q] ";
COLOR 8, 0
Print "QUIT"

LOCATE 23, 1
COLOR 7, 0
Print "           Select option: ";
COLOR 15, 0

LOCATE 25, 1
