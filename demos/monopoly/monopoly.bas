REM ============================================================================
REM MONOPOLY - Complete Board Game with AI
REM ============================================================================
REM 4 Players: 1 Human (Orange), 3 AI personalities
REM Full rules: properties, auctions, trades, jail, bankruptcy
REM ============================================================================

REM ANSI escape sequences
DIM ESC AS STRING
ESC = CHR$(27)

REM Colors
DIM C_RESET AS STRING
DIM C_RED AS STRING
DIM C_GREEN AS STRING
DIM C_YELLOW AS STRING
DIM C_BLUE AS STRING
DIM C_MAGENTA AS STRING
DIM C_CYAN AS STRING
DIM C_WHITE AS STRING
DIM C_ORANGE AS STRING
DIM C_BROWN AS STRING
DIM C_LBLUE AS STRING
DIM C_PINK AS STRING
C_RESET = "[0m"
C_RED = "[31m"
C_GREEN = "[32m"
C_YELLOW = "[33m"
C_BLUE = "[34m"
C_MAGENTA = "[35m"
C_CYAN = "[36m"
C_WHITE = "[37m"
C_ORANGE = "[38;5;208m"
C_BROWN = "[38;5;94m"
C_LBLUE = "[38;5;39m"
C_PINK = "[38;5;213m"

REM Board positions (40 spaces)
DIM BOARD_NAME(40) AS STRING
DIM BOARD_TYPE(40) AS INTEGER
DIM BOARD_GROUP(40) AS INTEGER
DIM BOARD_COST(40) AS INTEGER
DIM BOARD_RENT0(40) AS INTEGER
DIM BOARD_RENT1(40) AS INTEGER
DIM BOARD_RENT2(40) AS INTEGER
DIM BOARD_RENT3(40) AS INTEGER
DIM BOARD_RENT4(40) AS INTEGER
DIM BOARD_RENTHOTEL(40) AS INTEGER
DIM BOARD_HOUSECOST(40) AS INTEGER

REM Board types
DIM TYPE_GO AS INTEGER
DIM TYPE_PROPERTY AS INTEGER
DIM TYPE_CHANCE AS INTEGER
DIM TYPE_CHEST AS INTEGER
DIM TYPE_TAX AS INTEGER
DIM TYPE_RAILROAD AS INTEGER
DIM TYPE_UTILITY AS INTEGER
DIM TYPE_JAIL AS INTEGER
DIM TYPE_FREEPARKING AS INTEGER
DIM TYPE_GOTOJAIL AS INTEGER
TYPE_GO = 0
TYPE_PROPERTY = 1
TYPE_CHANCE = 2
TYPE_CHEST = 3
TYPE_TAX = 4
TYPE_RAILROAD = 5
TYPE_UTILITY = 6
TYPE_JAIL = 7
TYPE_FREEPARKING = 8
TYPE_GOTOJAIL = 9

REM Property groups
DIM GROUP_BROWN AS INTEGER
DIM GROUP_LBLUE AS INTEGER
DIM GROUP_PINK AS INTEGER
DIM GROUP_ORANGE AS INTEGER
DIM GROUP_RED AS INTEGER
DIM GROUP_YELLOW AS INTEGER
DIM GROUP_GREEN AS INTEGER
DIM GROUP_DBLUE AS INTEGER
DIM GROUP_RAILROAD AS INTEGER
DIM GROUP_UTILITY AS INTEGER
GROUP_BROWN = 1
GROUP_LBLUE = 2
GROUP_PINK = 3
GROUP_ORANGE = 4
GROUP_RED = 5
GROUP_YELLOW = 6
GROUP_GREEN = 7
GROUP_DBLUE = 8
GROUP_RAILROAD = 9
GROUP_UTILITY = 10

REM Property ownership and state
DIM PROP_OWNER(40) AS INTEGER
DIM PROP_HOUSES(40) AS INTEGER
DIM PROP_MORTGAGED(40) AS INTEGER

REM Player data (4 players: 0=human, 1-3=AI)
DIM PLAYER_NAME(4) AS STRING
DIM PLAYER_TOKEN(4) AS STRING
DIM PLAYER_MONEY(4) AS INTEGER
DIM PLAYER_POS(4) AS INTEGER
DIM PLAYER_INJAIL(4) AS INTEGER
DIM PLAYER_JAILTURN(4) AS INTEGER
DIM PLAYER_JAILCARD(4) AS INTEGER
DIM PLAYER_BANKRUPT(4) AS INTEGER
DIM PLAYER_ISAI(4) AS INTEGER
DIM PLAYER_STRATEGY(4) AS INTEGER

REM AI Strategies
DIM STRAT_AGGRESSIVE AS INTEGER
DIM STRAT_CONSERVATIVE AS INTEGER
DIM STRAT_BALANCED AS INTEGER
STRAT_AGGRESSIVE = 1
STRAT_CONSERVATIVE = 2
STRAT_BALANCED = 3

REM Game state
DIM currentPlayer AS INTEGER
DIM turnNumber AS INTEGER
DIM housesAvailable AS INTEGER
DIM hotelsAvailable AS INTEGER
DIM freeParkingPot AS INTEGER
DIM gameOver AS INTEGER
DIM lastDie1 AS INTEGER
DIM lastDie2 AS INTEGER
DIM doublesCount AS INTEGER

REM Game log (last 20 entries)
DIM gameLog(20) AS STRING
DIM logCount AS INTEGER

REM ============================================================================
REM TERMINAL HELPERS
REM ============================================================================

SUB ClearScreen()
    PRINT ESC; "[2J"; ESC; "[H";
END SUB

SUB GotoXY(row AS INTEGER, col AS INTEGER)
    PRINT ESC; "["; STR$(row); ";"; STR$(col); "H";
END SUB

SUB SetColor(clr AS STRING)
    PRINT ESC; clr;
END SUB

SUB ResetColor()
    PRINT ESC; C_RESET;
END SUB

SUB HideCursor()
    PRINT ESC; "[?25l";
END SUB

SUB ShowCursor()
    PRINT ESC; "[?25h";
END SUB

SUB WaitKey()
    DIM k AS STRING
    PRINT "Press any key...";
    INPUT k
END SUB

SUB Pause(ms AS INTEGER)
    DIM i AS INTEGER
    DIM j AS INTEGER
    FOR i = 1 TO ms / 10
        FOR j = 1 TO 1000
        NEXT j
    NEXT i
END SUB

REM ============================================================================
REM LOGGING
REM ============================================================================

SUB AddLog(msg AS STRING)
    DIM i AS INTEGER
    IF logCount >= 20 THEN
        FOR i = 0 TO 18
            gameLog(i) = gameLog(i + 1)
        NEXT i
        logCount = 19
    END IF
    gameLog(logCount) = msg
    logCount = logCount + 1
END SUB

REM ============================================================================
REM BOARD INITIALIZATION
REM ============================================================================

SUB InitBoard()
    REM Position 0: GO
    BOARD_NAME(0) = "GO"
    BOARD_TYPE(0) = TYPE_GO
    BOARD_GROUP(0) = 0
    BOARD_COST(0) = 0
    
    REM Position 1: Mediterranean Avenue
    BOARD_NAME(1) = "Mediterranean"
    BOARD_TYPE(1) = TYPE_PROPERTY
    BOARD_GROUP(1) = GROUP_BROWN
    BOARD_COST(1) = 60
    BOARD_RENT0(1) = 2
    BOARD_RENT1(1) = 10
    BOARD_RENT2(1) = 30
    BOARD_RENT3(1) = 90
    BOARD_RENT4(1) = 160
    BOARD_RENTHOTEL(1) = 250
    BOARD_HOUSECOST(1) = 50
    
    REM Position 2: Community Chest
    BOARD_NAME(2) = "Community Chest"
    BOARD_TYPE(2) = TYPE_CHEST
    BOARD_GROUP(2) = 0
    
    REM Position 3: Baltic Avenue
    BOARD_NAME(3) = "Baltic Ave"
    BOARD_TYPE(3) = TYPE_PROPERTY
    BOARD_GROUP(3) = GROUP_BROWN
    BOARD_COST(3) = 60
    BOARD_RENT0(3) = 4
    BOARD_RENT1(3) = 20
    BOARD_RENT2(3) = 60
    BOARD_RENT3(3) = 180
    BOARD_RENT4(3) = 320
    BOARD_RENTHOTEL(3) = 450
    BOARD_HOUSECOST(3) = 50
    
    REM Position 4: Income Tax
    BOARD_NAME(4) = "Income Tax"
    BOARD_TYPE(4) = TYPE_TAX
    BOARD_GROUP(4) = 0
    BOARD_COST(4) = 200
    
    REM Position 5: Reading Railroad
    BOARD_NAME(5) = "Reading RR"
    BOARD_TYPE(5) = TYPE_RAILROAD
    BOARD_GROUP(5) = GROUP_RAILROAD
    BOARD_COST(5) = 200
    
    REM Position 6: Oriental Avenue
    BOARD_NAME(6) = "Oriental Ave"
    BOARD_TYPE(6) = TYPE_PROPERTY
    BOARD_GROUP(6) = GROUP_LBLUE
    BOARD_COST(6) = 100
    BOARD_RENT0(6) = 6
    BOARD_RENT1(6) = 30
    BOARD_RENT2(6) = 90
    BOARD_RENT3(6) = 270
    BOARD_RENT4(6) = 400
    BOARD_RENTHOTEL(6) = 550
    BOARD_HOUSECOST(6) = 50
    
    REM Position 7: Chance
    BOARD_NAME(7) = "Chance"
    BOARD_TYPE(7) = TYPE_CHANCE
    BOARD_GROUP(7) = 0
    
    REM Position 8: Vermont Avenue
    BOARD_NAME(8) = "Vermont Ave"
    BOARD_TYPE(8) = TYPE_PROPERTY
    BOARD_GROUP(8) = GROUP_LBLUE
    BOARD_COST(8) = 100
    BOARD_RENT0(8) = 6
    BOARD_RENT1(8) = 30
    BOARD_RENT2(8) = 90
    BOARD_RENT3(8) = 270
    BOARD_RENT4(8) = 400
    BOARD_RENTHOTEL(8) = 550
    BOARD_HOUSECOST(8) = 50
    
    REM Position 9: Connecticut Avenue
    BOARD_NAME(9) = "Connecticut"
    BOARD_TYPE(9) = TYPE_PROPERTY
    BOARD_GROUP(9) = GROUP_LBLUE
    BOARD_COST(9) = 120
    BOARD_RENT0(9) = 8
    BOARD_RENT1(9) = 40
    BOARD_RENT2(9) = 100
    BOARD_RENT3(9) = 300
    BOARD_RENT4(9) = 450
    BOARD_RENTHOTEL(9) = 600
    BOARD_HOUSECOST(9) = 50
    
    REM Position 10: Jail
    BOARD_NAME(10) = "Jail"
    BOARD_TYPE(10) = TYPE_JAIL
    BOARD_GROUP(10) = 0
    
    REM Position 11: St. Charles Place
    BOARD_NAME(11) = "St. Charles"
    BOARD_TYPE(11) = TYPE_PROPERTY
    BOARD_GROUP(11) = GROUP_PINK
    BOARD_COST(11) = 140
    BOARD_RENT0(11) = 10
    BOARD_RENT1(11) = 50
    BOARD_RENT2(11) = 150
    BOARD_RENT3(11) = 450
    BOARD_RENT4(11) = 625
    BOARD_RENTHOTEL(11) = 750
    BOARD_HOUSECOST(11) = 100
    
    REM Position 12: Electric Company
    BOARD_NAME(12) = "Electric Co"
    BOARD_TYPE(12) = TYPE_UTILITY
    BOARD_GROUP(12) = GROUP_UTILITY
    BOARD_COST(12) = 150
    
    REM Position 13: States Avenue
    BOARD_NAME(13) = "States Ave"
    BOARD_TYPE(13) = TYPE_PROPERTY
    BOARD_GROUP(13) = GROUP_PINK
    BOARD_COST(13) = 140
    BOARD_RENT0(13) = 10
    BOARD_RENT1(13) = 50
    BOARD_RENT2(13) = 150
    BOARD_RENT3(13) = 450
    BOARD_RENT4(13) = 625
    BOARD_RENTHOTEL(13) = 750
    BOARD_HOUSECOST(13) = 100
    
    REM Position 14: Virginia Avenue
    BOARD_NAME(14) = "Virginia Ave"
    BOARD_TYPE(14) = TYPE_PROPERTY
    BOARD_GROUP(14) = GROUP_PINK
    BOARD_COST(14) = 160
    BOARD_RENT0(14) = 12
    BOARD_RENT1(14) = 60
    BOARD_RENT2(14) = 180
    BOARD_RENT3(14) = 500
    BOARD_RENT4(14) = 700
    BOARD_RENTHOTEL(14) = 900
    BOARD_HOUSECOST(14) = 100
    
    REM Position 15: Pennsylvania Railroad
    BOARD_NAME(15) = "Penn RR"
    BOARD_TYPE(15) = TYPE_RAILROAD
    BOARD_GROUP(15) = GROUP_RAILROAD
    BOARD_COST(15) = 200
    
    REM Position 16: St. James Place
    BOARD_NAME(16) = "St. James"
    BOARD_TYPE(16) = TYPE_PROPERTY
    BOARD_GROUP(16) = GROUP_ORANGE
    BOARD_COST(16) = 180
    BOARD_RENT0(16) = 14
    BOARD_RENT1(16) = 70
    BOARD_RENT2(16) = 200
    BOARD_RENT3(16) = 550
    BOARD_RENT4(16) = 750
    BOARD_RENTHOTEL(16) = 950
    BOARD_HOUSECOST(16) = 100
    
    REM Position 17: Community Chest
    BOARD_NAME(17) = "Community Chest"
    BOARD_TYPE(17) = TYPE_CHEST
    BOARD_GROUP(17) = 0
    
    REM Position 18: Tennessee Avenue
    BOARD_NAME(18) = "Tennessee"
    BOARD_TYPE(18) = TYPE_PROPERTY
    BOARD_GROUP(18) = GROUP_ORANGE
    BOARD_COST(18) = 180
    BOARD_RENT0(18) = 14
    BOARD_RENT1(18) = 70
    BOARD_RENT2(18) = 200
    BOARD_RENT3(18) = 550
    BOARD_RENT4(18) = 750
    BOARD_RENTHOTEL(18) = 950
    BOARD_HOUSECOST(18) = 100
    
    REM Position 19: New York Avenue
    BOARD_NAME(19) = "New York Ave"
    BOARD_TYPE(19) = TYPE_PROPERTY
    BOARD_GROUP(19) = GROUP_ORANGE
    BOARD_COST(19) = 200
    BOARD_RENT0(19) = 16
    BOARD_RENT1(19) = 80
    BOARD_RENT2(19) = 220
    BOARD_RENT3(19) = 600
    BOARD_RENT4(19) = 800
    BOARD_RENTHOTEL(19) = 1000
    BOARD_HOUSECOST(19) = 100
    
    REM Position 20: Free Parking
    BOARD_NAME(20) = "Free Parking"
    BOARD_TYPE(20) = TYPE_FREEPARKING
    BOARD_GROUP(20) = 0
    
    REM Position 21: Kentucky Avenue
    BOARD_NAME(21) = "Kentucky Ave"
    BOARD_TYPE(21) = TYPE_PROPERTY
    BOARD_GROUP(21) = GROUP_RED
    BOARD_COST(21) = 220
    BOARD_RENT0(21) = 18
    BOARD_RENT1(21) = 90
    BOARD_RENT2(21) = 250
    BOARD_RENT3(21) = 700
    BOARD_RENT4(21) = 875
    BOARD_RENTHOTEL(21) = 1050
    BOARD_HOUSECOST(21) = 150
    
    REM Position 22: Chance
    BOARD_NAME(22) = "Chance"
    BOARD_TYPE(22) = TYPE_CHANCE
    BOARD_GROUP(22) = 0
    
    REM Position 23: Indiana Avenue
    BOARD_NAME(23) = "Indiana Ave"
    BOARD_TYPE(23) = TYPE_PROPERTY
    BOARD_GROUP(23) = GROUP_RED
    BOARD_COST(23) = 220
    BOARD_RENT0(23) = 18
    BOARD_RENT1(23) = 90
    BOARD_RENT2(23) = 250
    BOARD_RENT3(23) = 700
    BOARD_RENT4(23) = 875
    BOARD_RENTHOTEL(23) = 1050
    BOARD_HOUSECOST(23) = 150
    
    REM Position 24: Illinois Avenue
    BOARD_NAME(24) = "Illinois Ave"
    BOARD_TYPE(24) = TYPE_PROPERTY
    BOARD_GROUP(24) = GROUP_RED
    BOARD_COST(24) = 240
    BOARD_RENT0(24) = 20
    BOARD_RENT1(24) = 100
    BOARD_RENT2(24) = 300
    BOARD_RENT3(24) = 750
    BOARD_RENT4(24) = 925
    BOARD_RENTHOTEL(24) = 1100
    BOARD_HOUSECOST(24) = 150
    
    REM Position 25: B&O Railroad
    BOARD_NAME(25) = "B&O RR"
    BOARD_TYPE(25) = TYPE_RAILROAD
    BOARD_GROUP(25) = GROUP_RAILROAD
    BOARD_COST(25) = 200
    
    REM Position 26: Atlantic Avenue
    BOARD_NAME(26) = "Atlantic Ave"
    BOARD_TYPE(26) = TYPE_PROPERTY
    BOARD_GROUP(26) = GROUP_YELLOW
    BOARD_COST(26) = 260
    BOARD_RENT0(26) = 22
    BOARD_RENT1(26) = 110
    BOARD_RENT2(26) = 330
    BOARD_RENT3(26) = 800
    BOARD_RENT4(26) = 975
    BOARD_RENTHOTEL(26) = 1150
    BOARD_HOUSECOST(26) = 150
    
    REM Position 27: Ventnor Avenue
    BOARD_NAME(27) = "Ventnor Ave"
    BOARD_TYPE(27) = TYPE_PROPERTY
    BOARD_GROUP(27) = GROUP_YELLOW
    BOARD_COST(27) = 260
    BOARD_RENT0(27) = 22
    BOARD_RENT1(27) = 110
    BOARD_RENT2(27) = 330
    BOARD_RENT3(27) = 800
    BOARD_RENT4(27) = 975
    BOARD_RENTHOTEL(27) = 1150
    BOARD_HOUSECOST(27) = 150
    
    REM Position 28: Water Works
    BOARD_NAME(28) = "Water Works"
    BOARD_TYPE(28) = TYPE_UTILITY
    BOARD_GROUP(28) = GROUP_UTILITY
    BOARD_COST(28) = 150
    
    REM Position 29: Marvin Gardens
    BOARD_NAME(29) = "Marvin Gardens"
    BOARD_TYPE(29) = TYPE_PROPERTY
    BOARD_GROUP(29) = GROUP_YELLOW
    BOARD_COST(29) = 280
    BOARD_RENT0(29) = 24
    BOARD_RENT1(29) = 120
    BOARD_RENT2(29) = 360
    BOARD_RENT3(29) = 850
    BOARD_RENT4(29) = 1025
    BOARD_RENTHOTEL(29) = 1200
    BOARD_HOUSECOST(29) = 150
    
    REM Position 30: Go To Jail
    BOARD_NAME(30) = "Go To Jail"
    BOARD_TYPE(30) = TYPE_GOTOJAIL
    BOARD_GROUP(30) = 0
    
    REM Position 31: Pacific Avenue
    BOARD_NAME(31) = "Pacific Ave"
    BOARD_TYPE(31) = TYPE_PROPERTY
    BOARD_GROUP(31) = GROUP_GREEN
    BOARD_COST(31) = 300
    BOARD_RENT0(31) = 26
    BOARD_RENT1(31) = 130
    BOARD_RENT2(31) = 390
    BOARD_RENT3(31) = 900
    BOARD_RENT4(31) = 1100
    BOARD_RENTHOTEL(31) = 1275
    BOARD_HOUSECOST(31) = 200
    
    REM Position 32: North Carolina Avenue
    BOARD_NAME(32) = "N. Carolina"
    BOARD_TYPE(32) = TYPE_PROPERTY
    BOARD_GROUP(32) = GROUP_GREEN
    BOARD_COST(32) = 300
    BOARD_RENT0(32) = 26
    BOARD_RENT1(32) = 130
    BOARD_RENT2(32) = 390
    BOARD_RENT3(32) = 900
    BOARD_RENT4(32) = 1100
    BOARD_RENTHOTEL(32) = 1275
    BOARD_HOUSECOST(32) = 200
    
    REM Position 33: Community Chest
    BOARD_NAME(33) = "Community Chest"
    BOARD_TYPE(33) = TYPE_CHEST
    BOARD_GROUP(33) = 0
    
    REM Position 34: Pennsylvania Avenue
    BOARD_NAME(34) = "Penn Ave"
    BOARD_TYPE(34) = TYPE_PROPERTY
    BOARD_GROUP(34) = GROUP_GREEN
    BOARD_COST(34) = 320
    BOARD_RENT0(34) = 28
    BOARD_RENT1(34) = 150
    BOARD_RENT2(34) = 450
    BOARD_RENT3(34) = 1000
    BOARD_RENT4(34) = 1200
    BOARD_RENTHOTEL(34) = 1400
    BOARD_HOUSECOST(34) = 200
    
    REM Position 35: Short Line Railroad
    BOARD_NAME(35) = "Short Line"
    BOARD_TYPE(35) = TYPE_RAILROAD
    BOARD_GROUP(35) = GROUP_RAILROAD
    BOARD_COST(35) = 200
    
    REM Position 36: Chance
    BOARD_NAME(36) = "Chance"
    BOARD_TYPE(36) = TYPE_CHANCE
    BOARD_GROUP(36) = 0
    
    REM Position 37: Park Place
    BOARD_NAME(37) = "Park Place"
    BOARD_TYPE(37) = TYPE_PROPERTY
    BOARD_GROUP(37) = GROUP_DBLUE
    BOARD_COST(37) = 350
    BOARD_RENT0(37) = 35
    BOARD_RENT1(37) = 175
    BOARD_RENT2(37) = 500
    BOARD_RENT3(37) = 1100
    BOARD_RENT4(37) = 1300
    BOARD_RENTHOTEL(37) = 1500
    BOARD_HOUSECOST(37) = 200
    
    REM Position 38: Luxury Tax
    BOARD_NAME(38) = "Luxury Tax"
    BOARD_TYPE(38) = TYPE_TAX
    BOARD_GROUP(38) = 0
    BOARD_COST(38) = 100
    
    REM Position 39: Boardwalk
    BOARD_NAME(39) = "Boardwalk"
    BOARD_TYPE(39) = TYPE_PROPERTY
    BOARD_GROUP(39) = GROUP_DBLUE
    BOARD_COST(39) = 400
    BOARD_RENT0(39) = 50
    BOARD_RENT1(39) = 200
    BOARD_RENT2(39) = 600
    BOARD_RENT3(39) = 1400
    BOARD_RENT4(39) = 1700
    BOARD_RENTHOTEL(39) = 2000
    BOARD_HOUSECOST(39) = 200
    
    REM Initialize property ownership
    DIM i AS INTEGER
    FOR i = 0 TO 39
        PROP_OWNER(i) = -1
        PROP_HOUSES(i) = 0
        PROP_MORTGAGED(i) = 0
    NEXT i
END SUB

REM ============================================================================
REM PLAYER INITIALIZATION
REM ============================================================================

SUB InitPlayers()
    REM Player 0: Human
    PLAYER_NAME(0) = "You"
    PLAYER_TOKEN(0) = "@"
    PLAYER_MONEY(0) = 1500
    PLAYER_POS(0) = 0
    PLAYER_INJAIL(0) = 0
    PLAYER_JAILTURN(0) = 0
    PLAYER_JAILCARD(0) = 0
    PLAYER_BANKRUPT(0) = 0
    PLAYER_ISAI(0) = 0
    PLAYER_STRATEGY(0) = 0
    
    REM Player 1: Andy (Aggressive)
    PLAYER_NAME(1) = "Andy"
    PLAYER_TOKEN(1) = "A"
    PLAYER_MONEY(1) = 1500
    PLAYER_POS(1) = 0
    PLAYER_INJAIL(1) = 0
    PLAYER_JAILTURN(1) = 0
    PLAYER_JAILCARD(1) = 0
    PLAYER_BANKRUPT(1) = 0
    PLAYER_ISAI(1) = 1
    PLAYER_STRATEGY(1) = STRAT_AGGRESSIVE
    
    REM Player 2: Betty (Conservative)
    PLAYER_NAME(2) = "Betty"
    PLAYER_TOKEN(2) = "B"
    PLAYER_MONEY(2) = 1500
    PLAYER_POS(2) = 0
    PLAYER_INJAIL(2) = 0
    PLAYER_JAILTURN(2) = 0
    PLAYER_JAILCARD(2) = 0
    PLAYER_BANKRUPT(2) = 0
    PLAYER_ISAI(2) = 1
    PLAYER_STRATEGY(2) = STRAT_CONSERVATIVE
    
    REM Player 3: Chip (Balanced)
    PLAYER_NAME(3) = "Chip"
    PLAYER_TOKEN(3) = "C"
    PLAYER_MONEY(3) = 1500
    PLAYER_POS(3) = 0
    PLAYER_INJAIL(3) = 0
    PLAYER_JAILTURN(3) = 0
    PLAYER_JAILCARD(3) = 0
    PLAYER_BANKRUPT(3) = 0
    PLAYER_ISAI(3) = 1
    PLAYER_STRATEGY(3) = STRAT_BALANCED
END SUB

REM ============================================================================
REM DICE ROLLING
REM ============================================================================

FUNCTION RollDie() AS INTEGER
    RollDie = INT(RND() * 6) + 1
END FUNCTION

SUB RollDice()
    lastDie1 = RollDie()
    lastDie2 = RollDie()
END SUB

FUNCTION IsDoubles() AS INTEGER
    IF lastDie1 = lastDie2 THEN
        IsDoubles = 1
    ELSE
        IsDoubles = 0
    END IF
END FUNCTION

FUNCTION DiceTotal() AS INTEGER
    DiceTotal = lastDie1 + lastDie2
END FUNCTION

REM ============================================================================
REM PROPERTY HELPERS
REM ============================================================================

FUNCTION CountOwnedInGroup(player AS INTEGER, grp AS INTEGER) AS INTEGER
    DIM cnt AS INTEGER
    DIM i AS INTEGER
    cnt = 0
    FOR i = 0 TO 39
        IF BOARD_GROUP(i) = grp THEN
            IF PROP_OWNER(i) = player THEN
                cnt = cnt + 1
            END IF
        END IF
    NEXT i
    CountOwnedInGroup = cnt
END FUNCTION

FUNCTION GroupSize(grp AS INTEGER) AS INTEGER
    IF grp = GROUP_BROWN THEN
        GroupSize = 2
    ELSEIF grp = GROUP_DBLUE THEN
        GroupSize = 2
    ELSEIF grp = GROUP_RAILROAD THEN
        GroupSize = 4
    ELSEIF grp = GROUP_UTILITY THEN
        GroupSize = 2
    ELSE
        GroupSize = 3
    END IF
END FUNCTION

FUNCTION HasMonopoly(player AS INTEGER, grp AS INTEGER) AS INTEGER
    IF CountOwnedInGroup(player, grp) = GroupSize(grp) THEN
        HasMonopoly = 1
    ELSE
        HasMonopoly = 0
    END IF
END FUNCTION

FUNCTION CalcRent(pos AS INTEGER, diceRoll AS INTEGER) AS INTEGER
    DIM owner AS INTEGER
    DIM rent AS INTEGER
    DIM houses AS INTEGER
    DIM grp AS INTEGER
    DIM rrCount AS INTEGER
    DIM utilCount AS INTEGER
    
    owner = PROP_OWNER(pos)
    IF owner < 0 THEN
        CalcRent = 0
        EXIT FUNCTION
    END IF
    
    IF PROP_MORTGAGED(pos) = 1 THEN
        CalcRent = 0
        EXIT FUNCTION
    END IF
    
    grp = BOARD_GROUP(pos)
    houses = PROP_HOUSES(pos)
    
    IF BOARD_TYPE(pos) = TYPE_RAILROAD THEN
        rrCount = CountOwnedInGroup(owner, GROUP_RAILROAD)
        IF rrCount = 1 THEN
            rent = 25
        ELSEIF rrCount = 2 THEN
            rent = 50
        ELSEIF rrCount = 3 THEN
            rent = 100
        ELSE
            rent = 200
        END IF
    ELSEIF BOARD_TYPE(pos) = TYPE_UTILITY THEN
        utilCount = CountOwnedInGroup(owner, GROUP_UTILITY)
        IF utilCount = 1 THEN
            rent = diceRoll * 4
        ELSE
            rent = diceRoll * 10
        END IF
    ELSE
        IF houses = 0 THEN
            rent = BOARD_RENT0(pos)
            IF HasMonopoly(owner, grp) = 1 THEN
                rent = rent * 2
            END IF
        ELSEIF houses = 1 THEN
            rent = BOARD_RENT1(pos)
        ELSEIF houses = 2 THEN
            rent = BOARD_RENT2(pos)
        ELSEIF houses = 3 THEN
            rent = BOARD_RENT3(pos)
        ELSEIF houses = 4 THEN
            rent = BOARD_RENT4(pos)
        ELSE
            rent = BOARD_RENTHOTEL(pos)
        END IF
    END IF
    
    CalcRent = rent
END FUNCTION

REM ============================================================================
REM DISPLAY
REM ============================================================================

SUB ShowStatus()
    DIM i AS INTEGER
    DIM props AS INTEGER
    
    GotoXY(1, 1)
    SetColor(C_WHITE)
    PRINT "=== MONOPOLY === Turn "; turnNumber; " ==="
    PRINT
    
    FOR i = 0 TO 3
        IF PLAYER_BANKRUPT(i) = 0 THEN
            IF i = currentPlayer THEN
                SetColor(C_YELLOW)
                PRINT ">> ";
            ELSE
                SetColor(C_WHITE)
                PRINT "   ";
            END IF
            
            IF i = 0 THEN
                SetColor(C_ORANGE)
            ELSEIF i = 1 THEN
                SetColor(C_GREEN)
            ELSEIF i = 2 THEN
                SetColor(C_RED)
            ELSE
                SetColor(C_MAGENTA)
            END IF
            
            props = CountPlayerProperties(i)
            PRINT PLAYER_NAME(i); " ["; PLAYER_TOKEN(i); "] $"; PLAYER_MONEY(i); " Props:"; props;
            IF PLAYER_INJAIL(i) = 1 THEN
                SetColor(C_RED)
                PRINT " [JAIL]";
            END IF
            PRINT
        ELSE
            SetColor(C_WHITE)
            PRINT "   "; PLAYER_NAME(i); " - BANKRUPT"
        END IF
    NEXT i
    
    ResetColor()
    PRINT
    PRINT "Free Parking Pot: $"; freeParkingPot
    PRINT "Houses: "; housesAvailable; " Hotels: "; hotelsAvailable
    PRINT
END SUB

FUNCTION CountPlayerProperties(player AS INTEGER) AS INTEGER
    DIM cnt AS INTEGER
    DIM i AS INTEGER
    cnt = 0
    FOR i = 0 TO 39
        IF PROP_OWNER(i) = player THEN
            cnt = cnt + 1
        END IF
    NEXT i
    CountPlayerProperties = cnt
END FUNCTION

SUB ShowPosition(player AS INTEGER)
    DIM pos AS INTEGER
    pos = PLAYER_POS(player)
    SetColor(C_CYAN)
    PRINT PLAYER_NAME(player); " is on: "; BOARD_NAME(pos)
    ResetColor()
END SUB

SUB ShowLog()
    DIM i AS INTEGER
    PRINT
    SetColor(C_WHITE)
    PRINT "--- Recent Events ---"
    FOR i = 0 TO logCount - 1
        PRINT gameLog(i)
    NEXT i
    ResetColor()
END SUB

REM ============================================================================
REM MOVEMENT
REM ============================================================================

SUB MovePlayer(player AS INTEGER, spaces AS INTEGER)
    DIM oldPos AS INTEGER
    DIM newPos AS INTEGER
    
    oldPos = PLAYER_POS(player)
    newPos = (oldPos + spaces) MOD 40
    
    REM Check if passed GO
    IF newPos < oldPos THEN
        IF PLAYER_INJAIL(player) = 0 THEN
            PLAYER_MONEY(player) = PLAYER_MONEY(player) + 200
            AddLog(PLAYER_NAME(player) + " passed GO, collected $200")
        END IF
    END IF
    
    PLAYER_POS(player) = newPos
END SUB

SUB SendToJail(player AS INTEGER)
    PLAYER_POS(player) = 10
    PLAYER_INJAIL(player) = 1
    PLAYER_JAILTURN(player) = 0
    AddLog(PLAYER_NAME(player) + " was sent to Jail!")
END SUB

REM ============================================================================
REM LANDING HANDLERS
REM ============================================================================

SUB HandleLanding(player AS INTEGER)
    DIM pos AS INTEGER
    DIM typ AS INTEGER
    DIM owner AS INTEGER
    DIM rent AS INTEGER
    
    pos = PLAYER_POS(player)
    typ = BOARD_TYPE(pos)
    
    IF typ = TYPE_GO THEN
        AddLog(PLAYER_NAME(player) + " landed on GO")
    ELSEIF typ = TYPE_PROPERTY THEN
        HandleProperty(player, pos)
    ELSEIF typ = TYPE_RAILROAD THEN
        HandleProperty(player, pos)
    ELSEIF typ = TYPE_UTILITY THEN
        HandleProperty(player, pos)
    ELSEIF typ = TYPE_CHANCE THEN
        HandleChance(player)
    ELSEIF typ = TYPE_CHEST THEN
        HandleChest(player)
    ELSEIF typ = TYPE_TAX THEN
        HandleTax(player, pos)
    ELSEIF typ = TYPE_JAIL THEN
        AddLog(PLAYER_NAME(player) + " is Just Visiting Jail")
    ELSEIF typ = TYPE_FREEPARKING THEN
        HandleFreeParking(player)
    ELSEIF typ = TYPE_GOTOJAIL THEN
        SendToJail(player)
    END IF
END SUB

SUB HandleProperty(player AS INTEGER, pos AS INTEGER)
    DIM owner AS INTEGER
    DIM rent AS INTEGER
    DIM cost AS INTEGER
    
    owner = PROP_OWNER(pos)
    
    IF owner < 0 THEN
        REM Unowned - offer to buy
        cost = BOARD_COST(pos)
        IF PLAYER_ISAI(player) = 1 THEN
            AIBuyDecision(player, pos)
        ELSE
            HumanBuyDecision(player, pos)
        END IF
    ELSEIF owner <> player THEN
        REM Pay rent
        rent = CalcRent(pos, DiceTotal())
        IF rent > 0 THEN
            PayRent(player, owner, rent, pos)
        END IF
    ELSE
        AddLog(PLAYER_NAME(player) + " landed on own property")
    END IF
END SUB

SUB PayRent(payer AS INTEGER, owner AS INTEGER, amount AS INTEGER, pos AS INTEGER)
    IF amount > PLAYER_MONEY(payer) THEN
        REM Try to raise money
        IF PLAYER_ISAI(payer) = 1 THEN
            AIRaiseMoney(payer, amount)
        ELSE
            HumanRaiseMoney(payer, amount)
        END IF
    END IF
    
    IF amount <= PLAYER_MONEY(payer) THEN
        PLAYER_MONEY(payer) = PLAYER_MONEY(payer) - amount
        PLAYER_MONEY(owner) = PLAYER_MONEY(owner) + amount
        AddLog(PLAYER_NAME(payer) + " paid $" + STR$(amount) + " rent to " + PLAYER_NAME(owner))
    ELSE
        REM Bankruptcy
        DoBankruptcy(payer, owner)
    END IF
END SUB

SUB HandleTax(player AS INTEGER, pos AS INTEGER)
    DIM tax AS INTEGER
    tax = BOARD_COST(pos)
    
    IF tax > PLAYER_MONEY(player) THEN
        IF PLAYER_ISAI(player) = 1 THEN
            AIRaiseMoney(player, tax)
        ELSE
            HumanRaiseMoney(player, tax)
        END IF
    END IF
    
    IF tax <= PLAYER_MONEY(player) THEN
        PLAYER_MONEY(player) = PLAYER_MONEY(player) - tax
        freeParkingPot = freeParkingPot + tax
        AddLog(PLAYER_NAME(player) + " paid $" + STR$(tax) + " tax")
    ELSE
        DoBankruptcy(player, -1)
    END IF
END SUB

SUB HandleFreeParking(player AS INTEGER)
    IF freeParkingPot > 0 THEN
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + freeParkingPot
        AddLog(PLAYER_NAME(player) + " collected $" + STR$(freeParkingPot) + " from Free Parking!")
        freeParkingPot = 0
    ELSE
        AddLog(PLAYER_NAME(player) + " landed on Free Parking")
    END IF
END SUB

SUB HandleChance(player AS INTEGER)
    DIM card AS INTEGER
    card = INT(RND() * 10)
    
    IF card = 0 THEN
        AddLog(PLAYER_NAME(player) + " drew: Advance to GO")
        PLAYER_POS(player) = 0
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 200
    ELSEIF card = 1 THEN
        AddLog(PLAYER_NAME(player) + " drew: Go to Jail")
        SendToJail(player)
    ELSEIF card = 2 THEN
        AddLog(PLAYER_NAME(player) + " drew: Bank pays $50")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 50
    ELSEIF card = 3 THEN
        AddLog(PLAYER_NAME(player) + " drew: Pay poor tax $15")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) - 15
        freeParkingPot = freeParkingPot + 15
    ELSEIF card = 4 THEN
        AddLog(PLAYER_NAME(player) + " drew: Advance to Illinois")
        IF PLAYER_POS(player) > 24 THEN
            PLAYER_MONEY(player) = PLAYER_MONEY(player) + 200
        END IF
        PLAYER_POS(player) = 24
        HandleLanding(player)
    ELSEIF card = 5 THEN
        AddLog(PLAYER_NAME(player) + " drew: Advance to Boardwalk")
        PLAYER_POS(player) = 39
        HandleLanding(player)
    ELSEIF card = 6 THEN
        AddLog(PLAYER_NAME(player) + " drew: Collect $150")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 150
    ELSEIF card = 7 THEN
        AddLog(PLAYER_NAME(player) + " drew: Go back 3 spaces")
        PLAYER_POS(player) = PLAYER_POS(player) - 3
        IF PLAYER_POS(player) < 0 THEN
            PLAYER_POS(player) = PLAYER_POS(player) + 40
        END IF
        HandleLanding(player)
    ELSEIF card = 8 THEN
        AddLog(PLAYER_NAME(player) + " drew: Get Out of Jail Free")
        PLAYER_JAILCARD(player) = PLAYER_JAILCARD(player) + 1
    ELSE
        AddLog(PLAYER_NAME(player) + " drew: Collect $100")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 100
    END IF
END SUB

SUB HandleChest(player AS INTEGER)
    DIM card AS INTEGER
    card = INT(RND() * 10)
    
    IF card = 0 THEN
        AddLog(PLAYER_NAME(player) + " drew: Advance to GO")
        PLAYER_POS(player) = 0
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 200
    ELSEIF card = 1 THEN
        AddLog(PLAYER_NAME(player) + " drew: Bank error, collect $200")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 200
    ELSEIF card = 2 THEN
        AddLog(PLAYER_NAME(player) + " drew: Doctor's fee $50")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) - 50
        freeParkingPot = freeParkingPot + 50
    ELSEIF card = 3 THEN
        AddLog(PLAYER_NAME(player) + " drew: Go to Jail")
        SendToJail(player)
    ELSEIF card = 4 THEN
        AddLog(PLAYER_NAME(player) + " drew: Holiday fund $100")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 100
    ELSEIF card = 5 THEN
        AddLog(PLAYER_NAME(player) + " drew: Income tax refund $20")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 20
    ELSEIF card = 6 THEN
        AddLog(PLAYER_NAME(player) + " drew: Life insurance $100")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 100
    ELSEIF card = 7 THEN
        AddLog(PLAYER_NAME(player) + " drew: Hospital fees $100")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) - 100
        freeParkingPot = freeParkingPot + 100
    ELSEIF card = 8 THEN
        AddLog(PLAYER_NAME(player) + " drew: Get Out of Jail Free")
        PLAYER_JAILCARD(player) = PLAYER_JAILCARD(player) + 1
    ELSE
        AddLog(PLAYER_NAME(player) + " drew: Inherit $100")
        PLAYER_MONEY(player) = PLAYER_MONEY(player) + 100
    END IF
END SUB

REM ============================================================================
REM BUYING DECISIONS
REM ============================================================================

SUB HumanBuyDecision(player AS INTEGER, pos AS INTEGER)
    DIM choice AS STRING
    DIM cost AS INTEGER
    
    cost = BOARD_COST(pos)
    
    PRINT
    SetColor(C_CYAN)
    PRINT BOARD_NAME(pos); " is available for $"; cost
    ResetColor()
    
    IF PLAYER_MONEY(player) >= cost THEN
        PRINT "Buy? (Y/N): ";
        INPUT choice
        IF choice = "Y" THEN
            PLAYER_MONEY(player) = PLAYER_MONEY(player) - cost
            PROP_OWNER(pos) = player
            AddLog(PLAYER_NAME(player) + " bought " + BOARD_NAME(pos) + " for $" + STR$(cost))
        ELSEIF choice = "y" THEN
            PLAYER_MONEY(player) = PLAYER_MONEY(player) - cost
            PROP_OWNER(pos) = player
            AddLog(PLAYER_NAME(player) + " bought " + BOARD_NAME(pos) + " for $" + STR$(cost))
        ELSE
            AddLog(PLAYER_NAME(player) + " declined to buy " + BOARD_NAME(pos))
            DoAuction(pos)
        END IF
    ELSE
        PRINT "You can't afford this property."
        DoAuction(pos)
    END IF
END SUB

SUB AIBuyDecision(player AS INTEGER, pos AS INTEGER)
    DIM cost AS INTEGER
    DIM shouldBuy AS INTEGER
    DIM grp AS INTEGER
    
    cost = BOARD_COST(pos)
    grp = BOARD_GROUP(pos)
    shouldBuy = 0
    
    IF PLAYER_STRATEGY(player) = STRAT_AGGRESSIVE THEN
        REM Andy buys everything he can afford
        IF PLAYER_MONEY(player) >= cost THEN
            shouldBuy = 1
        END IF
    ELSEIF PLAYER_STRATEGY(player) = STRAT_CONSERVATIVE THEN
        REM Betty only buys if has $500+ reserve after
        IF PLAYER_MONEY(player) >= cost + 500 THEN
            REM Only target orange, red, railroads
            IF grp = GROUP_ORANGE THEN
                shouldBuy = 1
            ELSEIF grp = GROUP_RED THEN
                shouldBuy = 1
            ELSEIF grp = GROUP_RAILROAD THEN
                shouldBuy = 1
            END IF
        END IF
    ELSE
        REM Chip uses balanced approach
        IF PLAYER_MONEY(player) >= cost + 200 THEN
            shouldBuy = 1
        ELSEIF CountOwnedInGroup(player, grp) > 0 THEN
            REM Always complete sets if possible
            IF PLAYER_MONEY(player) >= cost THEN
                shouldBuy = 1
            END IF
        END IF
    END IF
    
    IF shouldBuy = 1 THEN
        PLAYER_MONEY(player) = PLAYER_MONEY(player) - cost
        PROP_OWNER(pos) = player
        AddLog(PLAYER_NAME(player) + " bought " + BOARD_NAME(pos) + " for $" + STR$(cost))
    ELSE
        AddLog(PLAYER_NAME(player) + " declined to buy " + BOARD_NAME(pos))
        DoAuction(pos)
    END IF
END SUB

REM ============================================================================
REM AUCTIONS
REM ============================================================================

SUB DoAuction(pos AS INTEGER)
    DIM bids(4) AS INTEGER
    DIM passed(4) AS INTEGER
    DIM highBid AS INTEGER
    DIM highBidder AS INTEGER
    DIM allPassed AS INTEGER
    DIM i AS INTEGER
    DIM bid AS INTEGER
    DIM bidStr AS STRING
    
    AddLog("Auction started for " + BOARD_NAME(pos))
    highBid = 0
    highBidder = -1
    
    FOR i = 0 TO 3
        bids(i) = 0
        passed(i) = 0
        IF PLAYER_BANKRUPT(i) = 1 THEN
            passed(i) = 1
        END IF
    NEXT i
    
    WHILE 1 = 1
        allPassed = 1
        FOR i = 0 TO 3
            IF passed(i) = 0 THEN
                IF PLAYER_BANKRUPT(i) = 0 THEN
                    allPassed = 0
                    
                    IF PLAYER_ISAI(i) = 1 THEN
                        bid = AIGetBid(i, pos, highBid)
                        IF bid > highBid THEN
                            IF bid <= PLAYER_MONEY(i) THEN
                                highBid = bid
                                highBidder = i
                                AddLog(PLAYER_NAME(i) + " bids $" + STR$(bid))
                            ELSE
                                passed(i) = 1
                            END IF
                        ELSE
                            passed(i) = 1
                        END IF
                    ELSE
                        PRINT
                        PRINT "Current high bid: $"; highBid; " by ";
                        IF highBidder >= 0 THEN
                            PRINT PLAYER_NAME(highBidder)
                        ELSE
                            PRINT "none"
                        END IF
                        PRINT "Your bid (0 to pass, max $"; PLAYER_MONEY(i); "): ";
                        INPUT bidStr
                        bid = VAL(bidStr)
                        IF bid > highBid THEN
                            IF bid <= PLAYER_MONEY(i) THEN
                                highBid = bid
                                highBidder = i
                                AddLog(PLAYER_NAME(i) + " bids $" + STR$(bid))
                            ELSE
                                PRINT "You can't afford that bid."
                                passed(i) = 1
                            END IF
                        ELSE
                            passed(i) = 1
                        END IF
                    END IF
                END IF
            END IF
        NEXT i
        
        IF allPassed = 1 THEN
            EXIT WHILE
        END IF
    WEND
    
    IF highBidder >= 0 THEN
        PLAYER_MONEY(highBidder) = PLAYER_MONEY(highBidder) - highBid
        PROP_OWNER(pos) = highBidder
        AddLog(PLAYER_NAME(highBidder) + " won auction for $" + STR$(highBid))
    ELSE
        AddLog("No bids - property remains unsold")
    END IF
END SUB

FUNCTION AIGetBid(player AS INTEGER, pos AS INTEGER, currentBid AS INTEGER) AS INTEGER
    DIM maxBid AS INTEGER
    DIM cost AS INTEGER
    DIM grp AS INTEGER
    
    cost = BOARD_COST(pos)
    grp = BOARD_GROUP(pos)
    
    IF PLAYER_STRATEGY(player) = STRAT_AGGRESSIVE THEN
        REM Andy bids up to 150% of value
        maxBid = cost * 3 / 2
        IF CountOwnedInGroup(player, grp) > 0 THEN
            maxBid = cost * 2
        END IF
    ELSEIF PLAYER_STRATEGY(player) = STRAT_CONSERVATIVE THEN
        REM Betty bids max 80% of value
        maxBid = cost * 4 / 5
    ELSE
        REM Chip bids based on value
        maxBid = cost
        IF CountOwnedInGroup(player, grp) > 0 THEN
            maxBid = cost * 5 / 4
        END IF
    END IF
    
    IF maxBid > PLAYER_MONEY(player) - 100 THEN
        maxBid = PLAYER_MONEY(player) - 100
    END IF
    
    IF currentBid < maxBid THEN
        IF currentBid = 0 THEN
            AIGetBid = 10
        ELSE
            AIGetBid = currentBid + 10
        END IF
    ELSE
        AIGetBid = 0
    END IF
END FUNCTION

REM ============================================================================
REM JAIL HANDLING
REM ============================================================================

SUB HandleJailTurn(player AS INTEGER)
    DIM choice AS STRING
    
    IF PLAYER_ISAI(player) = 1 THEN
        AIJailDecision(player)
    ELSE
        PRINT
        SetColor(C_RED)
        PRINT "You are in JAIL! (Turn "; PLAYER_JAILTURN(player) + 1; " of 3)"
        ResetColor()
        PRINT "Options:"
        PRINT "1. Try to roll doubles"
        IF PLAYER_MONEY(player) >= 50 THEN
            PRINT "2. Pay $50 to get out"
        END IF
        IF PLAYER_JAILCARD(player) > 0 THEN
            PRINT "3. Use Get Out of Jail Free card"
        END IF
        PRINT "Choice: ";
        INPUT choice
        
        IF choice = "1" THEN
            RollDice()
            PRINT "You rolled "; lastDie1; " and "; lastDie2
            IF IsDoubles() = 1 THEN
                PLAYER_INJAIL(player) = 0
                AddLog(PLAYER_NAME(player) + " rolled doubles and escaped jail!")
                MovePlayer(player, DiceTotal())
                HandleLanding(player)
            ELSE
                PLAYER_JAILTURN(player) = PLAYER_JAILTURN(player) + 1
                IF PLAYER_JAILTURN(player) >= 3 THEN
                    PLAYER_MONEY(player) = PLAYER_MONEY(player) - 50
                    PLAYER_INJAIL(player) = 0
                    AddLog(PLAYER_NAME(player) + " paid $50 after 3 turns and left jail")
                    MovePlayer(player, DiceTotal())
                    HandleLanding(player)
                ELSE
                    AddLog(PLAYER_NAME(player) + " failed to roll doubles")
                END IF
            END IF
        ELSEIF choice = "2" THEN
            IF PLAYER_MONEY(player) >= 50 THEN
                PLAYER_MONEY(player) = PLAYER_MONEY(player) - 50
                PLAYER_INJAIL(player) = 0
                AddLog(PLAYER_NAME(player) + " paid $50 to leave jail")
                RollDice()
                MovePlayer(player, DiceTotal())
                HandleLanding(player)
            END IF
        ELSEIF choice = "3" THEN
            IF PLAYER_JAILCARD(player) > 0 THEN
                PLAYER_JAILCARD(player) = PLAYER_JAILCARD(player) - 1
                PLAYER_INJAIL(player) = 0
                AddLog(PLAYER_NAME(player) + " used Get Out of Jail Free card")
                RollDice()
                MovePlayer(player, DiceTotal())
                HandleLanding(player)
            END IF
        END IF
    END IF
END SUB

SUB AIJailDecision(player AS INTEGER)
    IF PLAYER_STRATEGY(player) = STRAT_AGGRESSIVE THEN
        REM Andy always pays immediately
        IF PLAYER_MONEY(player) >= 50 THEN
            PLAYER_MONEY(player) = PLAYER_MONEY(player) - 50
            PLAYER_INJAIL(player) = 0
            AddLog(PLAYER_NAME(player) + " paid $50 to leave jail")
            RollDice()
            MovePlayer(player, DiceTotal())
            HandleLanding(player)
            EXIT SUB
        END IF
    END IF
    
    REM Try to roll doubles first
    IF PLAYER_JAILCARD(player) > 0 THEN
        IF PLAYER_JAILTURN(player) >= 2 THEN
            PLAYER_JAILCARD(player) = PLAYER_JAILCARD(player) - 1
            PLAYER_INJAIL(player) = 0
            AddLog(PLAYER_NAME(player) + " used Get Out of Jail Free card")
            RollDice()
            MovePlayer(player, DiceTotal())
            HandleLanding(player)
            EXIT SUB
        END IF
    END IF
    
    RollDice()
    AddLog(PLAYER_NAME(player) + " rolled " + STR$(lastDie1) + " and " + STR$(lastDie2))
    IF IsDoubles() = 1 THEN
        PLAYER_INJAIL(player) = 0
        AddLog(PLAYER_NAME(player) + " rolled doubles and escaped jail!")
        MovePlayer(player, DiceTotal())
        HandleLanding(player)
    ELSE
        PLAYER_JAILTURN(player) = PLAYER_JAILTURN(player) + 1
        IF PLAYER_JAILTURN(player) >= 3 THEN
            PLAYER_MONEY(player) = PLAYER_MONEY(player) - 50
            PLAYER_INJAIL(player) = 0
            AddLog(PLAYER_NAME(player) + " paid $50 after 3 turns")
            MovePlayer(player, DiceTotal())
            HandleLanding(player)
        END IF
    END IF
END SUB

REM ============================================================================
REM BANKRUPTCY
REM ============================================================================

SUB DoBankruptcy(player AS INTEGER, creditor AS INTEGER)
    DIM i AS INTEGER
    
    AddLog(PLAYER_NAME(player) + " is BANKRUPT!")
    PLAYER_BANKRUPT(player) = 1
    
    IF creditor >= 0 THEN
        REM Transfer all assets to creditor
        PLAYER_MONEY(creditor) = PLAYER_MONEY(creditor) + PLAYER_MONEY(player)
        FOR i = 0 TO 39
            IF PROP_OWNER(i) = player THEN
                PROP_OWNER(i) = creditor
                housesAvailable = housesAvailable + PROP_HOUSES(i)
                PROP_HOUSES(i) = 0
            END IF
        NEXT i
        IF PLAYER_JAILCARD(player) > 0 THEN
            PLAYER_JAILCARD(creditor) = PLAYER_JAILCARD(creditor) + PLAYER_JAILCARD(player)
        END IF
    ELSE
        REM Return assets to bank
        FOR i = 0 TO 39
            IF PROP_OWNER(i) = player THEN
                PROP_OWNER(i) = -1
                housesAvailable = housesAvailable + PROP_HOUSES(i)
                PROP_HOUSES(i) = 0
                PROP_MORTGAGED(i) = 0
            END IF
        NEXT i
    END IF
    
    PLAYER_MONEY(player) = 0
    CheckGameOver()
END SUB

SUB CheckGameOver()
    DIM alive AS INTEGER
    DIM winner AS INTEGER
    DIM i AS INTEGER
    
    alive = 0
    winner = -1
    FOR i = 0 TO 3
        IF PLAYER_BANKRUPT(i) = 0 THEN
            alive = alive + 1
            winner = i
        END IF
    NEXT i
    
    IF alive = 1 THEN
        gameOver = 1
        ClearScreen()
        SetColor(C_YELLOW)
        PRINT "======================================="
        PRINT "         GAME OVER!"
        PRINT "======================================="
        PRINT
        SetColor(C_GREEN)
        PRINT PLAYER_NAME(winner); " WINS!"
        PRINT
        PRINT "Final Money: $"; PLAYER_MONEY(winner)
        PRINT "Properties: "; CountPlayerProperties(winner)
        ResetColor()
    END IF
END SUB

REM ============================================================================
REM RAISE MONEY
REM ============================================================================

SUB HumanRaiseMoney(player AS INTEGER, needed AS INTEGER)
    DIM choice AS STRING
    DIM propNum AS INTEGER
    DIM i AS INTEGER
    
    WHILE PLAYER_MONEY(player) < needed
        PRINT
        SetColor(C_RED)
        PRINT "You need $"; needed; " but only have $"; PLAYER_MONEY(player)
        ResetColor()
        PRINT "Your properties:"
        FOR i = 0 TO 39
            IF PROP_OWNER(i) = player THEN
                PRINT i; ": "; BOARD_NAME(i);
                IF PROP_MORTGAGED(i) = 1 THEN
                    PRINT " [MORTGAGED]";
                ELSEIF PROP_HOUSES(i) > 0 THEN
                    PRINT " ["; PROP_HOUSES(i); " houses]";
                END IF
                PRINT
            END IF
        NEXT i
        PRINT
        PRINT "M # - Mortgage property #"
        PRINT "S # - Sell house from property #"
        PRINT "B   - Declare bankruptcy"
        PRINT "Choice: ";
        INPUT choice
        
        IF LEFT$(choice, 1) = "M" THEN
            propNum = VAL(MID$(choice, 3))
            IF PROP_OWNER(propNum) = player THEN
                IF PROP_MORTGAGED(propNum) = 0 THEN
                    IF PROP_HOUSES(propNum) = 0 THEN
                        PROP_MORTGAGED(propNum) = 1
                        PLAYER_MONEY(player) = PLAYER_MONEY(player) + BOARD_COST(propNum) / 2
                        AddLog(PLAYER_NAME(player) + " mortgaged " + BOARD_NAME(propNum))
                    ELSE
                        PRINT "Must sell houses first!"
                    END IF
                ELSE
                    PRINT "Already mortgaged!"
                END IF
            END IF
        ELSEIF LEFT$(choice, 1) = "m" THEN
            propNum = VAL(MID$(choice, 3))
            IF PROP_OWNER(propNum) = player THEN
                IF PROP_MORTGAGED(propNum) = 0 THEN
                    IF PROP_HOUSES(propNum) = 0 THEN
                        PROP_MORTGAGED(propNum) = 1
                        PLAYER_MONEY(player) = PLAYER_MONEY(player) + BOARD_COST(propNum) / 2
                        AddLog(PLAYER_NAME(player) + " mortgaged " + BOARD_NAME(propNum))
                    END IF
                END IF
            END IF
        ELSEIF LEFT$(choice, 1) = "S" THEN
            propNum = VAL(MID$(choice, 3))
            IF PROP_OWNER(propNum) = player THEN
                IF PROP_HOUSES(propNum) > 0 THEN
                    PROP_HOUSES(propNum) = PROP_HOUSES(propNum) - 1
                    housesAvailable = housesAvailable + 1
                    PLAYER_MONEY(player) = PLAYER_MONEY(player) + BOARD_HOUSECOST(propNum) / 2
                    AddLog(PLAYER_NAME(player) + " sold house from " + BOARD_NAME(propNum))
                END IF
            END IF
        ELSEIF LEFT$(choice, 1) = "s" THEN
            propNum = VAL(MID$(choice, 3))
            IF PROP_OWNER(propNum) = player THEN
                IF PROP_HOUSES(propNum) > 0 THEN
                    PROP_HOUSES(propNum) = PROP_HOUSES(propNum) - 1
                    housesAvailable = housesAvailable + 1
                    PLAYER_MONEY(player) = PLAYER_MONEY(player) + BOARD_HOUSECOST(propNum) / 2
                    AddLog(PLAYER_NAME(player) + " sold house from " + BOARD_NAME(propNum))
                END IF
            END IF
        ELSEIF choice = "B" THEN
            EXIT WHILE
        ELSEIF choice = "b" THEN
            EXIT WHILE
        END IF
    WEND
END SUB

SUB AIRaiseMoney(player AS INTEGER, needed AS INTEGER)
    DIM i AS INTEGER
    DIM maxHouses AS INTEGER
    DIM maxProp AS INTEGER
    
    WHILE PLAYER_MONEY(player) < needed
        REM First sell houses
        maxHouses = 0
        maxProp = -1
        FOR i = 0 TO 39
            IF PROP_OWNER(i) = player THEN
                IF PROP_HOUSES(i) > maxHouses THEN
                    maxHouses = PROP_HOUSES(i)
                    maxProp = i
                END IF
            END IF
        NEXT i
        
        IF maxProp >= 0 THEN
            PROP_HOUSES(maxProp) = PROP_HOUSES(maxProp) - 1
            housesAvailable = housesAvailable + 1
            PLAYER_MONEY(player) = PLAYER_MONEY(player) + BOARD_HOUSECOST(maxProp) / 2
            AddLog(PLAYER_NAME(player) + " sold house from " + BOARD_NAME(maxProp))
        ELSE
            REM Then mortgage
            FOR i = 0 TO 39
                IF PROP_OWNER(i) = player THEN
                    IF PROP_MORTGAGED(i) = 0 THEN
                        PROP_MORTGAGED(i) = 1
                        PLAYER_MONEY(player) = PLAYER_MONEY(player) + BOARD_COST(i) / 2
                        AddLog(PLAYER_NAME(player) + " mortgaged " + BOARD_NAME(i))
                        EXIT FOR
                    END IF
                END IF
            NEXT i
            IF i > 39 THEN
                EXIT WHILE
            END IF
        END IF
    WEND
END SUB

REM ============================================================================
REM BUILDING
REM ============================================================================

SUB HumanBuildMenu(player AS INTEGER)
    DIM choice AS STRING
    DIM propNum AS INTEGER
    DIM i AS INTEGER
    DIM grp AS INTEGER
    DIM canBuild AS INTEGER
    
    PRINT
    PRINT "Your properties with monopolies:"
    FOR i = 0 TO 39
        IF PROP_OWNER(i) = player THEN
            IF BOARD_TYPE(i) = TYPE_PROPERTY THEN
                grp = BOARD_GROUP(i)
                IF HasMonopoly(player, grp) = 1 THEN
                    PRINT i; ": "; BOARD_NAME(i); " - "; PROP_HOUSES(i); " houses, $"; BOARD_HOUSECOST(i); "/house"
                END IF
            END IF
        END IF
    NEXT i
    
    PRINT
    PRINT "Enter property # to build on (0 to cancel): ";
    INPUT choice
    propNum = VAL(choice)
    
    IF propNum > 0 THEN
        IF PROP_OWNER(propNum) = player THEN
            grp = BOARD_GROUP(propNum)
            IF HasMonopoly(player, grp) = 1 THEN
                IF PROP_MORTGAGED(propNum) = 0 THEN
                    IF PROP_HOUSES(propNum) < 5 THEN
                        IF PLAYER_MONEY(player) >= BOARD_HOUSECOST(propNum) THEN
                            IF housesAvailable > 0 THEN
                                PROP_HOUSES(propNum) = PROP_HOUSES(propNum) + 1
                                housesAvailable = housesAvailable - 1
                                PLAYER_MONEY(player) = PLAYER_MONEY(player) - BOARD_HOUSECOST(propNum)
                                AddLog(PLAYER_NAME(player) + " built house on " + BOARD_NAME(propNum))
                            ELSE
                                PRINT "No houses available!"
                            END IF
                        ELSE
                            PRINT "Not enough money!"
                        END IF
                    ELSE
                        PRINT "Already has hotel!"
                    END IF
                ELSE
                    PRINT "Property is mortgaged!"
                END IF
            ELSE
                PRINT "You don't have a monopoly!"
            END IF
        END IF
    END IF
END SUB

SUB AIBuildHouses(player AS INTEGER)
    DIM i AS INTEGER
    DIM grp AS INTEGER
    DIM minHouses AS INTEGER
    DIM buildProp AS INTEGER
    
    REM Find property with fewest houses in a monopoly
    WHILE 1 = 1
        buildProp = -1
        minHouses = 99
        
        FOR i = 0 TO 39
            IF PROP_OWNER(i) = player THEN
                IF BOARD_TYPE(i) = TYPE_PROPERTY THEN
                    grp = BOARD_GROUP(i)
                    IF HasMonopoly(player, grp) = 1 THEN
                        IF PROP_MORTGAGED(i) = 0 THEN
                            IF PROP_HOUSES(i) < 5 THEN
                                IF PROP_HOUSES(i) < minHouses THEN
                                    minHouses = PROP_HOUSES(i)
                                    buildProp = i
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        NEXT i
        
        IF buildProp < 0 THEN
            EXIT WHILE
        END IF
        
        IF PLAYER_MONEY(player) < BOARD_HOUSECOST(buildProp) + 200 THEN
            EXIT WHILE
        END IF
        
        IF housesAvailable = 0 THEN
            EXIT WHILE
        END IF
        
        PROP_HOUSES(buildProp) = PROP_HOUSES(buildProp) + 1
        housesAvailable = housesAvailable - 1
        PLAYER_MONEY(player) = PLAYER_MONEY(player) - BOARD_HOUSECOST(buildProp)
        AddLog(PLAYER_NAME(player) + " built house on " + BOARD_NAME(buildProp))
    WEND
END SUB

REM ============================================================================
REM TURN PROCESSING
REM ============================================================================

SUB ProcessTurn(player AS INTEGER)
    DIM choice AS STRING
    
    IF PLAYER_BANKRUPT(player) = 1 THEN
        EXIT SUB
    END IF
    
    ClearScreen()
    ShowStatus()
    ShowPosition(player)
    
    IF PLAYER_INJAIL(player) = 1 THEN
        HandleJailTurn(player)
        EXIT SUB
    END IF
    
    IF PLAYER_ISAI(player) = 1 THEN
        REM AI turn
        Pause(500)
        RollDice()
        AddLog(PLAYER_NAME(player) + " rolled " + STR$(lastDie1) + " + " + STR$(lastDie2) + " = " + STR$(DiceTotal()))
        MovePlayer(player, DiceTotal())
        ShowPosition(player)
        HandleLanding(player)
        
        REM AI builds if possible
        AIBuildHouses(player)
    ELSE
        REM Human turn
        PRINT
        PRINT "Options:"
        PRINT "R - Roll dice"
        PRINT "B - Build houses"
        PRINT "V - View properties"
        PRINT "Q - Quit game"
        PRINT "Choice: ";
        INPUT choice
        
        IF choice = "R" THEN
            RollDice()
            PRINT "You rolled "; lastDie1; " + "; lastDie2; " = "; DiceTotal()
            AddLog(PLAYER_NAME(player) + " rolled " + STR$(lastDie1) + " + " + STR$(lastDie2))
            MovePlayer(player, DiceTotal())
            ShowPosition(player)
            HandleLanding(player)
        ELSEIF choice = "r" THEN
            RollDice()
            PRINT "You rolled "; lastDie1; " + "; lastDie2; " = "; DiceTotal()
            AddLog(PLAYER_NAME(player) + " rolled " + STR$(lastDie1) + " + " + STR$(lastDie2))
            MovePlayer(player, DiceTotal())
            ShowPosition(player)
            HandleLanding(player)
        ELSEIF choice = "B" THEN
            HumanBuildMenu(player)
            ProcessTurn(player)
        ELSEIF choice = "b" THEN
            HumanBuildMenu(player)
            ProcessTurn(player)
        ELSEIF choice = "V" THEN
            ShowAllProperties()
            ProcessTurn(player)
        ELSEIF choice = "v" THEN
            ShowAllProperties()
            ProcessTurn(player)
        ELSEIF choice = "Q" THEN
            gameOver = 1
        ELSEIF choice = "q" THEN
            gameOver = 1
        ELSE
            ProcessTurn(player)
        END IF
    END IF
END SUB

SUB ShowAllProperties()
    DIM i AS INTEGER
    
    ClearScreen()
    PRINT "=== ALL PROPERTIES ==="
    PRINT
    FOR i = 0 TO 39
        IF BOARD_TYPE(i) = TYPE_PROPERTY THEN
            PRINT i; ": "; BOARD_NAME(i); " ($"; BOARD_COST(i); ")";
            IF PROP_OWNER(i) >= 0 THEN
                PRINT " - "; PLAYER_NAME(PROP_OWNER(i));
                IF PROP_HOUSES(i) > 0 THEN
                    IF PROP_HOUSES(i) = 5 THEN
                        PRINT " [HOTEL]";
                    ELSE
                        PRINT " ["; PROP_HOUSES(i); "H]";
                    END IF
                END IF
                IF PROP_MORTGAGED(i) = 1 THEN
                    PRINT " [M]";
                END IF
            ELSE
                PRINT " - FOR SALE";
            END IF
            PRINT
        ELSEIF BOARD_TYPE(i) = TYPE_RAILROAD THEN
            PRINT i; ": "; BOARD_NAME(i); " ($200)";
            IF PROP_OWNER(i) >= 0 THEN
                PRINT " - "; PLAYER_NAME(PROP_OWNER(i));
            ELSE
                PRINT " - FOR SALE";
            END IF
            PRINT
        ELSEIF BOARD_TYPE(i) = TYPE_UTILITY THEN
            PRINT i; ": "; BOARD_NAME(i); " ($150)";
            IF PROP_OWNER(i) >= 0 THEN
                PRINT " - "; PLAYER_NAME(PROP_OWNER(i));
            ELSE
                PRINT " - FOR SALE";
            END IF
            PRINT
        END IF
    NEXT i
    PRINT
    WaitKey()
END SUB

REM ============================================================================
REM MAIN GAME LOOP
REM ============================================================================

SUB GameLoop()
    DIM i AS INTEGER
    
    WHILE gameOver = 0
        turnNumber = turnNumber + 1
        
        FOR i = 0 TO 3
            IF gameOver = 0 THEN
                currentPlayer = i
                ProcessTurn(i)
            END IF
        NEXT i
    WEND
END SUB

REM ============================================================================
REM INITIALIZATION AND MAIN
REM ============================================================================

SUB InitGame()
    InitBoard()
    InitPlayers()
    
    currentPlayer = 0
    turnNumber = 0
    housesAvailable = 32
    hotelsAvailable = 12
    freeParkingPot = 0
    gameOver = 0
    logCount = 0
    doublesCount = 0
END SUB

SUB ShowTitle()
    ClearScreen()
    SetColor(C_GREEN)
    PRINT "======================================="
    PRINT "           M O N O P O L Y"
    PRINT "======================================="
    ResetColor()
    PRINT
    PRINT "Players:"
    SetColor(C_ORANGE)
    PRINT "  You (Human) - Orange token"
    SetColor(C_GREEN)
    PRINT "  Andy (AI Aggressive) - Green token"
    SetColor(C_RED)
    PRINT "  Betty (AI Conservative) - Red token"
    SetColor(C_MAGENTA)
    PRINT "  Chip (AI Balanced) - Purple token"
    ResetColor()
    PRINT
    PRINT "Press Enter to start..."
    DIM dummy AS STRING
    INPUT dummy
END SUB

SUB Main()
    ShowTitle()
    InitGame()
    GameLoop()
    
    PRINT
    PRINT "Thanks for playing Monopoly!"
    ShowCursor()
END SUB

Main()
