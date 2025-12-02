' monopoly.bas - Viper Monopoly Main Game
' A complete Monopoly implementation featuring:
' - Full 40-space board with all properties, railroads, and utilities
' - Complete Chance and Community Chest card decks
' - 4 players: 1 human + 3 AI with distinct personalities
' - AI strategies: Aggressive (Andy), Conservative (Betty), Balanced (Chip)
' - Full rules: buying, auctions, rent, building, mortgaging, jail, bankruptcy
' - Save/load game functionality
'
' Uses Viper.* runtime features:
' - Viper.Collections.List, Viper.Text.StringBuilder
' - Viper.String, Viper.Random, Viper.Math
' - Viper.Time, Viper.IO.File, Viper.Console
' - Viper.Convert, Viper.Object

' Include game files
AddFile "constants.bas"
AddFile "property.bas"
AddFile "player.bas"
AddFile "card.bas"
AddFile "board.bas"
AddFile "ai.bas"
AddFile "display.bas"
AddFile "game.bas"

' Main program
DIM game AS MonopolyGame
DIM choice AS STRING
DIM display AS GameDisplay

' Initialize display
display = NEW GameDisplay()
display.Init()

' Show title screen
display.ClearScreen()
display.SetColor(CLR_BRIGHT_YELLOW, CLR_BLACK)
LOCATE 5, 20
PRINT "**********************************"
LOCATE 6, 20
PRINT "*                                *"
LOCATE 7, 20
PRINT "*       VIPER MONOPOLY           *"
LOCATE 8, 20
PRINT "*                                *"
LOCATE 9, 20
PRINT "*   A Complete Board Game        *"
LOCATE 10, 20
PRINT "*   with Sophisticated AI        *"
LOCATE 11, 20
PRINT "*                                *"
LOCATE 12, 20
PRINT "**********************************"

display.SetColor(CLR_WHITE, CLR_BLACK)
LOCATE 15, 20
PRINT "Players:"
display.SetColor(TOKEN_ORANGE, CLR_BLACK)
LOCATE 16, 22
PRINT "O - You (Human)"
display.SetColor(TOKEN_GREEN, CLR_BLACK)
LOCATE 17, 22
PRINT "A - Andy (Aggressive AI)"
display.SetColor(TOKEN_RED, CLR_BLACK)
LOCATE 18, 22
PRINT "B - Betty (Conservative AI)"
display.SetColor(TOKEN_PURPLE, CLR_BLACK)
LOCATE 19, 22
PRINT "C - Chip (Balanced AI)"

display.SetColor(CLR_BRIGHT_CYAN, CLR_BLACK)
LOCATE 22, 20
PRINT "[N] New Game"
LOCATE 23, 20
PRINT "[L] Load Game"
LOCATE 24, 20
PRINT "[Q] Quit"

display.SetColor(CLR_GRAY, CLR_BLACK)
LOCATE 27, 20
PRINT "Features: Full board, complete rules,"
LOCATE 28, 20
PRINT "trading, auctions, save/load, AI personalities"

display.ResetColor()
LOCATE 30, 20
PRINT "Your choice: ";

choice = UCASE$(display.WaitForKey())

IF choice = "Q" THEN
    display.ClearScreen()
    PRINT "Thanks for playing Viper Monopoly!"
    END
END IF

' Initialize game
game = NEW MonopolyGame()
game.Init()

IF choice = "L" THEN
    ' Try to load saved game
    DIM loadFile AS STRING
    display.ShowMessage("Enter save file name (or press Enter for 'monopoly_save.txt'): ")
    loadFile = display.GetInput("")
    IF LEN(loadFile) = 0 THEN
        loadFile = "monopoly_save.txt"
    END IF
    ' Note: LoadGame will fail silently if file doesn't exist
    ' In a full implementation we'd check file existence first
    game.LoadGame(loadFile)
    display.ShowMessage("Game loaded (if file existed)")
    display.Pause(1000)
END IF

' Run the game
game.RunGame()

' End
display.ClearScreen()
PRINT
PRINT "Thank you for playing Viper Monopoly!"
PRINT
PRINT "Game Statistics:"
PRINT "  Total turns played: "; STR$(game.turnNumber)
PRINT "  Properties bought: "; STR$(game.totalPropertiesBought)
PRINT "  Houses built: "; STR$(game.totalHousesBuilt)
PRINT "  Total rent collected: $"; STR$(game.totalRentPaid)
PRINT

END

