' display.bas - Board display and UI for Monopoly
' ASCII art board rendering and status display

' Display manager class
CLASS GameDisplay
    DIM boardWidth AS INTEGER
    DIM boardHeight AS INTEGER

    SUB Init()
        boardWidth = 80
        boardHeight = 40
    END SUB

    ' Clear screen and reset cursor
    SUB ClearScreen()
        CLS
    END SUB

    ' Set text color
    SUB SetColor(fg AS INTEGER, bg AS INTEGER)
        COLOR fg, bg
    END SUB

    ' Reset to default colors
    SUB ResetColor()
        COLOR CLR_WHITE, CLR_BLACK
    END SUB

    ' Draw the main title
    SUB DrawTitle()
        Me.SetColor(CLR_BRIGHT_YELLOW, CLR_BLACK)
        LOCATE 1, 25
        PRINT "*** VIPER MONOPOLY ***"
        Me.ResetColor()
    END SUB

    ' Draw a single board space
    SUB DrawSpace(row AS INTEGER, col AS INTEGER, spaceName AS STRING, spaceColor AS INTEGER, tokens AS STRING)
        DIM displayName AS STRING
        DIM i AS INTEGER

        ' Truncate name to fit
        displayName = spaceName
        IF LEN(displayName) > 10 THEN
            displayName = LEFT$(displayName, 9)
        END IF

        LOCATE row, col
        Me.SetColor(spaceColor, CLR_BLACK)
        PRINT displayName;

        ' Show player tokens if any
        IF LEN(tokens) > 0 THEN
            LOCATE row + 1, col
            Me.SetColor(CLR_WHITE, CLR_BLACK)
            PRINT tokens;
        END IF

        Me.ResetColor()
    END SUB

    ' Draw the full game board
    SUB DrawBoard(board AS GameBoard, players() AS Player, numPlayers AS INTEGER)
        DIM i AS INTEGER
        DIM pos AS INTEGER
        DIM row AS INTEGER
        DIM col AS INTEGER
        DIM sp AS BoardSpace
        DIM prop AS GameProperty
        DIM spaceColor AS INTEGER
        DIM tokens AS STRING
        DIM p AS INTEGER

        Me.ClearScreen()
        Me.DrawTitle()

        ' Draw top row (spaces 20-30)
        row = 3
        FOR i = 20 TO 30
            col = 3 + (i - 20) * 7
            sp = board.GetSpace(i)
            spaceColor = Me.GetSpaceColor(sp, board)

            ' Get tokens on this space
            tokens = ""
            FOR p = 0 TO numPlayers - 1
                IF players(p).IsBankrupt() = 0 THEN
                    IF players(p).GetPosition() = i THEN
                        tokens = tokens + players(p).GetToken()
                    END IF
                END IF
            NEXT p

            Me.DrawSpace(row, col, sp.GetName(), spaceColor, tokens)
        NEXT i

        ' Draw left column (spaces 10-19, going up)
        col = 3
        FOR i = 19 TO 11 STEP -1
            row = 5 + (19 - i) * 2
            sp = board.GetSpace(i)
            spaceColor = Me.GetSpaceColor(sp, board)

            tokens = ""
            FOR p = 0 TO numPlayers - 1
                IF players(p).IsBankrupt() = 0 THEN
                    IF players(p).GetPosition() = i THEN
                        tokens = tokens + players(p).GetToken()
                    END IF
                END IF
            NEXT p

            Me.DrawSpace(row, col, sp.GetName(), spaceColor, tokens)
        NEXT i

        ' Draw Jail (space 10)
        row = 23
        sp = board.GetSpace(10)
        tokens = ""
        FOR p = 0 TO numPlayers - 1
            IF players(p).IsBankrupt() = 0 THEN
                IF players(p).GetPosition() = 10 THEN
                    tokens = tokens + players(p).GetToken()
                END IF
            END IF
        NEXT p
        Me.DrawSpace(row, col, sp.GetName(), CLR_WHITE, tokens)

        ' Draw right column (spaces 31-39, going down)
        col = 73
        FOR i = 31 TO 39
            row = 5 + (i - 31) * 2
            sp = board.GetSpace(i)
            spaceColor = Me.GetSpaceColor(sp, board)

            tokens = ""
            FOR p = 0 TO numPlayers - 1
                IF players(p).IsBankrupt() = 0 THEN
                    IF players(p).GetPosition() = i THEN
                        tokens = tokens + players(p).GetToken()
                    END IF
                END IF
            NEXT p

            Me.DrawSpace(row, col, sp.GetName(), spaceColor, tokens)
        NEXT i

        ' Draw bottom row (spaces 0-9)
        row = 23
        FOR i = 9 TO 0 STEP -1
            col = 3 + (9 - i) * 7
            sp = board.GetSpace(i)
            spaceColor = Me.GetSpaceColor(sp, board)

            tokens = ""
            FOR p = 0 TO numPlayers - 1
                IF players(p).IsBankrupt() = 0 THEN
                    IF players(p).GetPosition() = i THEN
                        tokens = tokens + players(p).GetToken()
                    END IF
                END IF
            NEXT p

            Me.DrawSpace(row, col, sp.GetName(), spaceColor, tokens)
        NEXT i
    END SUB

    ' Get color for a board space
    FUNCTION GetSpaceColor(sp AS BoardSpace, board AS GameBoard) AS INTEGER
        DIM spType AS INTEGER
        DIM propIdx AS INTEGER
        DIM prop AS GameProperty

        spType = sp.GetType()

        IF spType = SPACE_GO THEN GetSpaceColor = CLR_BRIGHT_GREEN
        IF spType = SPACE_JAIL THEN GetSpaceColor = CLR_WHITE
        IF spType = SPACE_FREEPARKING THEN GetSpaceColor = CLR_BRIGHT_WHITE
        IF spType = SPACE_GOTOJAIL THEN GetSpaceColor = CLR_BRIGHT_RED
        IF spType = SPACE_CHANCE THEN GetSpaceColor = CLR_BRIGHT_MAGENTA
        IF spType = SPACE_CHEST THEN GetSpaceColor = CLR_BRIGHT_CYAN
        IF spType = SPACE_TAX THEN GetSpaceColor = CLR_YELLOW

        IF sp.IsProperty() = 1 THEN
            propIdx = sp.GetPropertyIndex()
            IF propIdx >= 0 THEN
                prop = board.GetProperty(propIdx)
                GetSpaceColor = prop.GetGroupColor()
            END IF
        END IF
    END FUNCTION

    ' Draw player status panel
    SUB DrawPlayerStatus(players() AS Player, numPlayers AS INTEGER, currentPlayer AS INTEGER)
        DIM i AS INTEGER
        DIM row AS INTEGER
        DIM statusLine AS STRING

        row = 26
        Me.SetColor(CLR_BRIGHT_WHITE, CLR_BLACK)
        LOCATE row, 3
        PRINT "=== PLAYER STATUS ===";

        FOR i = 0 TO numPlayers - 1
            row = row + 1
            LOCATE row, 3

            IF i = currentPlayer THEN
                Me.SetColor(CLR_BRIGHT_YELLOW, CLR_BLACK)
                PRINT "> ";
            ELSE
                PRINT "  ";
            END IF

            Me.SetColor(players(i).GetColor(), CLR_BLACK)
            statusLine = players(i).GetStatusString()
            PRINT statusLine;

            IF players(i).IsAI() = 1 THEN
                Me.SetColor(CLR_GRAY, CLR_BLACK)
                PRINT " [AI]";
            END IF

            Me.ResetColor()
        NEXT i
    END SUB

    ' Draw dice roll result
    SUB DrawDiceRoll(die1 AS INTEGER, die2 AS INTEGER)
        DIM total AS INTEGER
        total = die1 + die2

        LOCATE 32, 3
        Me.SetColor(CLR_BRIGHT_WHITE, CLR_BLACK)
        PRINT "Dice: [";
        Me.SetColor(CLR_BRIGHT_YELLOW, CLR_BLACK)
        PRINT STR$(die1);
        Me.SetColor(CLR_BRIGHT_WHITE, CLR_BLACK)
        PRINT "] [";
        Me.SetColor(CLR_BRIGHT_YELLOW, CLR_BLACK)
        PRINT STR$(die2);
        Me.SetColor(CLR_BRIGHT_WHITE, CLR_BLACK)
        PRINT "] = ";
        Me.SetColor(CLR_BRIGHT_GREEN, CLR_BLACK)
        PRINT STR$(total);

        IF die1 = die2 THEN
            Me.SetColor(CLR_BRIGHT_MAGENTA, CLR_BLACK)
            PRINT " DOUBLES!";
        END IF

        Me.ResetColor()
    END SUB

    ' Draw action menu for human player
    SUB DrawActionMenu(canBuy AS INTEGER, canBuild AS INTEGER, canTrade AS INTEGER, canMortgage AS INTEGER)
        DIM row AS INTEGER
        row = 34

        Me.SetColor(CLR_BRIGHT_WHITE, CLR_BLACK)
        LOCATE row, 3
        PRINT "=== ACTIONS ===";

        row = row + 1
        LOCATE row, 3
        PRINT "[R] Roll Dice   ";

        IF canBuy = 1 THEN
            PRINT "[B] Buy Property   ";
        END IF

        IF canBuild = 1 THEN
            PRINT "[H] Build House   ";
        END IF

        row = row + 1
        LOCATE row, 3

        IF canTrade = 1 THEN
            PRINT "[T] Trade   ";
        END IF

        IF canMortgage = 1 THEN
            PRINT "[M] Mortgage   ";
        END IF

        PRINT "[S] Save   [Q] Quit";
        Me.ResetColor()
    END SUB

    ' Display a message
    SUB ShowMessage(msg AS STRING)
        LOCATE 37, 3
        Me.SetColor(CLR_BRIGHT_CYAN, CLR_BLACK)
        PRINT msg;
        Me.ResetColor()
    END SUB

    ' Display an error message
    SUB ShowError(msg AS STRING)
        LOCATE 37, 3
        Me.SetColor(CLR_BRIGHT_RED, CLR_BLACK)
        PRINT "ERROR: "; msg;
        Me.ResetColor()
    END SUB

    ' Show property details
    SUB ShowPropertyDetails(prop AS GameProperty, board AS GameBoard, ownerName AS STRING)
        DIM row AS INTEGER
        row = 38

        Me.SetColor(prop.GetGroupColor(), CLR_BLACK)
        LOCATE row, 3
        PRINT prop.GetName();

        Me.SetColor(CLR_WHITE, CLR_BLACK)
        PRINT " - Cost: $"; STR$(prop.GetCost());
        PRINT " Rent: $"; STR$(prop.GetBaseRent());

        IF LEN(ownerName) > 0 THEN
            PRINT " Owner: "; ownerName;
        ELSE
            PRINT " (Unowned)";
        END IF

        Me.ResetColor()
    END SUB

    ' Show card drawn
    SUB ShowCard(deckName AS STRING, cardText AS STRING)
        LOCATE 39, 3
        Me.SetColor(CLR_BRIGHT_MAGENTA, CLR_BLACK)
        PRINT deckName; ": ";
        Me.SetColor(CLR_BRIGHT_WHITE, CLR_BLACK)
        PRINT cardText;
        Me.ResetColor()
    END SUB

    ' Show AI thinking message
    SUB ShowAIThinking(playerName AS STRING)
        LOCATE 37, 3
        Me.SetColor(CLR_GRAY, CLR_BLACK)
        PRINT playerName; " is thinking...";
        Me.ResetColor()
    END SUB

    ' Show AI decision
    SUB ShowAIDecision(playerName AS STRING, decision AS STRING, comment AS STRING)
        LOCATE 37, 3
        Me.SetColor(CLR_BRIGHT_GREEN, CLR_BLACK)
        PRINT playerName; ": "; decision;

        IF LEN(comment) > 0 THEN
            Me.SetColor(CLR_GRAY, CLR_BLACK)
            PRINT " ("; comment; ")";
        END IF

        Me.ResetColor()
    END SUB

    ' Display game over screen
    SUB ShowGameOver(winnerName AS STRING, finalMoney AS INTEGER)
        Me.ClearScreen()

        LOCATE 10, 25
        Me.SetColor(CLR_BRIGHT_YELLOW, CLR_BLACK)
        PRINT "*** GAME OVER ***"

        LOCATE 14, 20
        Me.SetColor(CLR_BRIGHT_GREEN, CLR_BLACK)
        PRINT winnerName; " WINS!"

        LOCATE 16, 20
        Me.SetColor(CLR_WHITE, CLR_BLACK)
        PRINT "Final Net Worth: $"; STR$(finalMoney)

        LOCATE 20, 20
        PRINT "Thanks for playing Viper Monopoly!"

        LOCATE 24, 20
        Me.SetColor(CLR_GRAY, CLR_BLACK)
        PRINT "Press any key to exit..."

        Me.ResetColor()
    END SUB

    ' Display trade offer
    SUB ShowTradeOffer(fromName AS STRING, toName AS STRING, offerDesc AS STRING, wantDesc AS STRING)
        LOCATE 37, 3
        Me.SetColor(CLR_BRIGHT_CYAN, CLR_BLACK)
        PRINT "TRADE: "; fromName; " offers "; toName; ":"

        LOCATE 38, 3
        Me.SetColor(CLR_WHITE, CLR_BLACK)
        PRINT "  Offering: "; offerDesc

        LOCATE 39, 3
        PRINT "  Wanting: "; wantDesc

        Me.ResetColor()
    END SUB

    ' Wait for key press
    FUNCTION WaitForKey() AS STRING
        DIM k AS STRING
        k = INKEY$()
        DO WHILE k = ""
            k = INKEY$()
            SLEEP 50
        LOOP
        WaitForKey = k
    END FUNCTION

    ' Get input from user
    FUNCTION GetInput(prompt AS STRING) AS STRING
        DIM inp AS STRING
        LOCATE 40, 3
        Me.SetColor(CLR_BRIGHT_WHITE, CLR_BLACK)
        PRINT prompt;
        Me.ResetColor()
        INPUT inp
        GetInput = inp
    END FUNCTION

    ' Pause for effect
    SUB Pause(ms AS INTEGER)
        SLEEP ms
    END SUB
END CLASS

