' ╔══════════════════════════════════════════════════════════╗
' ║  vTRIS - Advanced Tetris Demo for Viper BASIC          ║
' ║  Featuring: Levels, High Scores, Colorful Graphics     ║
' ╚══════════════════════════════════════════════════════════╝

AddFile "pieces.bas"
AddFile "board.bas"
AddFile "scoreboard.bas"

' === GLOBAL VARIABLES ===
Dim GameBoard As Board
Dim CurrentPiece As Piece
Dim NextPiece As Piece
Dim ScoreBoard As Scoreboard
Dim GameScore As Integer
Dim GameLines As Integer
Dim GameLevel As Integer
Dim GameOver As Integer
Dim DropCounter As Integer
Dim DropSpeed As Integer

' === MAIN MENU ===
Sub ShowMainMenu()
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
End Sub

Sub ShowInstructions()
    CLS

    LOCATE 2, 1
    COLOR 14, 0
    Print "╔════════════════════════════════════════════════════╗"
    LOCATE 3, 1
    Print "║                ";
    COLOR 15, 0
    Print "INSTRUCTIONS";
    COLOR 14, 0
    Print "                      ║"
    LOCATE 4, 1
    Print "╚════════════════════════════════════════════════════╝"

    LOCATE 6, 1
    COLOR 11, 0
    Print "  OBJECTIVE:";
    COLOR 7, 0
    Print " Complete horizontal lines to score points"

    LOCATE 8, 1
    COLOR 11, 0
    Print "  CONTROLS:"
    LOCATE 9, 1
    COLOR 15, 0
    Print "    A/D";
    COLOR 7, 0
    Print " - Move piece left/right"
    LOCATE 10, 1
    COLOR 15, 0
    Print "    W";
    COLOR 7, 0
    Print "   - Rotate piece clockwise"
    LOCATE 11, 1
    COLOR 15, 0
    Print "    S";
    COLOR 7, 0
    Print "   - Soft drop (move down faster)"
    LOCATE 12, 1
    COLOR 15, 0
    Print "    Q";
    COLOR 7, 0
    Print "   - Quit to main menu"

    LOCATE 14, 1
    COLOR 11, 0
    Print "  SCORING:"
    LOCATE 15, 1
    COLOR 7, 0
    Print "    1 Line  = 100 points"
    LOCATE 16, 1
    Print "    2 Lines = 400 points"
    LOCATE 17, 1
    Print "    3 Lines = 900 points"
    LOCATE 18, 1
    Print "    4 Lines = 1600 points"

    LOCATE 20, 1
    COLOR 11, 0
    Print "  LEVELS:";
    COLOR 7, 0
    Print " Speed increases every 10 lines"

    LOCATE 23, 1
    COLOR 8, 0
    Print "Press any key to return to menu...";
    COLOR 7, 0

    Dim k As String
    k = ""
    While k = ""
        k = Inkey$()
    Wend
End Sub

Sub ShowHighScores()
    CLS
    ScoreBoard.DrawScoreboard(3)

    LOCATE 23, 1
    COLOR 8, 0
    Print "Press any key to return to menu..."
    COLOR 7, 0

    Dim k As String
    k = ""
    While k = ""
        k = Inkey$()
    Wend
End Sub

' === GAME INITIALIZATION ===
Sub InitGame()
    GameBoard = New Board()
    ScoreBoard = New Scoreboard()
    GameScore = 0
    GameLines = 0
    GameLevel = 1
    GameOver = 0
    DropSpeed = 30
    DropCounter = 0

    ' Create first and next pieces
    Dim pieceType As Integer
    pieceType = Int(Rnd() * 7)
    CurrentPiece = New Piece(pieceType)
    CurrentPiece.PosX = 4
    CurrentPiece.PosY = 0

    pieceType = Int(Rnd() * 7)
    NextPiece = New Piece(pieceType)
End Sub

Sub SpawnPiece()
    ' Create new piece based on next piece type
    Dim pieceType As Integer
    pieceType = NextPiece.PieceType
    CurrentPiece = New Piece(pieceType)
    CurrentPiece.PosX = 4
    CurrentPiece.PosY = 0

    ' Check if can spawn (game over if not)
    If GameBoard.CanPlace(CurrentPiece) = 0 Then
        GameOver = 1
    End If

    ' Generate new next piece
    pieceType = Int(Rnd() * 7)
    NextPiece = New Piece(pieceType)
End Sub

Sub DrawUI()
    ' Right side panel
    LOCATE 2, 26
    COLOR 14, 0
    Print "╔══════════════════╗"

    LOCATE 3, 26
    Print "║ ";
    COLOR 15, 0
    Print "vTRIS v2.0";
    COLOR 14, 0
    Print "      ║"

    LOCATE 4, 26
    Print "╠══════════════════╣"

    ' Score
    LOCATE 5, 26
    COLOR 14, 0
    Print "║ ";
    COLOR 11, 0
    Print "SCORE:";
    COLOR 14, 0
    Print "          ║"
    LOCATE 6, 26
    Print "║ ";
    COLOR 15, 0
    Print GameScore; "            ";
    COLOR 14, 0
    Print "║"

    ' Lines
    LOCATE 7, 26
    Print "║ ";
    COLOR 10, 0
    Print "LINES:";
    COLOR 14, 0
    Print "          ║"
    LOCATE 8, 26
    Print "║ ";
    COLOR 15, 0
    Print GameLines; "            ";
    COLOR 14, 0
    Print "║"

    ' Level
    LOCATE 9, 26
    Print "║ ";
    COLOR 9, 0
    Print "LEVEL:";
    COLOR 14, 0
    Print "          ║"
    LOCATE 10, 26
    Print "║ ";
    COLOR 15, 0
    Print GameLevel; "            ";
    COLOR 14, 0
    Print "║"

    LOCATE 11, 26
    Print "╠══════════════════╣"

    ' Next piece preview
    LOCATE 12, 26
    Print "║ ";
    COLOR 13, 0
    Print "NEXT:";
    COLOR 14, 0
    Print "           ║"

    ' Draw next piece preview (rows 13-16)
    Dim i As Integer, j As Integer
    For i = 0 To 3
        LOCATE 13 + i, 26
        COLOR 14, 0
        Print "║  ";

        For j = 0 To 3
            If NextPiece.Shape(i, j) = 1 Then
                COLOR NextPiece.PieceColor, 0
                Print "██";
            Else
                COLOR 7, 0
                Print "  ";
            End If
        Next j

        COLOR 14, 0
        Print "  ║"
    Next i

    LOCATE 17, 26
    Print "╠══════════════════╣"

    ' Controls
    LOCATE 18, 26
    Print "║ ";
    COLOR 8, 0
    Print "A/D - Move";
    COLOR 14, 0
    Print "     ║"
    LOCATE 19, 26
    Print "║ ";
    COLOR 8, 0
    Print "W - Rotate";
    COLOR 14, 0
    Print "      ║"
    LOCATE 20, 26
    Print "║ ";
    COLOR 8, 0
    Print "S - Drop";
    COLOR 14, 0
    Print "        ║"
    LOCATE 21, 26
    Print "║ ";
    COLOR 8, 0
    Print "Q - Quit";
    COLOR 14, 0
    Print "        ║"

    LOCATE 22, 26
    Print "╚══════════════════╝"

    COLOR 7, 0
End Sub

Sub UpdateLevel()
    ' Increase level every 10 lines
    Dim newLevel As Integer
    newLevel = Int(GameLines / 10) + 1

    If newLevel > GameLevel Then
        GameLevel = newLevel

        ' Increase speed (decrease drop delay)
        DropSpeed = 30 - (GameLevel * 2)
        If DropSpeed < 5 Then
            DropSpeed = 5
        End If
    End If
End Sub

Sub LockPiece()
    Dim linesCleared As Integer

    GameBoard.PlacePiece(CurrentPiece)
    linesCleared = GameBoard.CheckLines()

    If linesCleared > 0 Then
        ' Score based on lines cleared
        GameScore = GameScore + (linesCleared * linesCleared * 100)
        GameLines = GameLines + linesCleared
        UpdateLevel()
    End If

    SpawnPiece()
End Sub

' === MAIN GAME LOOP ===
Sub GameLoop()
    Dim k As String

    CLS

    While GameOver = 0
        ' Get input
        k = Inkey$()

        ' Handle input
        If k = "a" Or k = "A" Then
            CurrentPiece.MoveLeft()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.MoveRight()
            End If
        End If

        If k = "d" Or k = "D" Then
            CurrentPiece.MoveRight()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.MoveLeft()
            End If
        End If

        If k = "w" Or k = "W" Then
            CurrentPiece.RotateClockwise()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                ' Undo rotation
                CurrentPiece.RotateClockwise()
                CurrentPiece.RotateClockwise()
                CurrentPiece.RotateClockwise()
            End If
        End If

        If k = "s" Or k = "S" Then
            CurrentPiece.MoveDown()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.PosY = CurrentPiece.PosY - 1
                LockPiece()
            End If
        End If

        If k = "q" Or k = "Q" Then
            GameOver = 1
        End If

        ' Auto drop
        DropCounter = DropCounter + 1
        If DropCounter >= DropSpeed Then
            DropCounter = 0
            CurrentPiece.MoveDown()

            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.PosY = CurrentPiece.PosY - 1
                LockPiece()
            End If
        End If

        ' Draw everything
        GameBoard.DrawBoard()
        GameBoard.DrawPiece(CurrentPiece)
        DrawUI()

        Sleep 50
    Wend
End Sub

Sub GameOverScreen()
    CLS

    LOCATE 8, 1
    COLOR 12, 0
    Print "     ╔════════════════════════════════════════╗"
    LOCATE 9, 1
    Print "     ║                                        ║"
    LOCATE 10, 1
    Print "     ║           ";
    COLOR 15, 0
    Print "GAME OVER!";
    COLOR 12, 0
    Print "                 ║"
    LOCATE 11, 1
    Print "     ║                                        ║"
    LOCATE 12, 1
    Print "     ╚════════════════════════════════════════╝"

    LOCATE 14, 1
    COLOR 11, 0
    Print "            Final Score: ";
    COLOR 15, 0
    Print GameScore

    LOCATE 15, 1
    COLOR 10, 0
    Print "            Lines Cleared: ";
    COLOR 15, 0
    Print GameLines

    LOCATE 16, 1
    COLOR 9, 0
    Print "            Level Reached: ";
    COLOR 15, 0
    Print GameLevel

    ' Check for high score
    If ScoreBoard.IsHighScore(GameScore) = 1 Then
        LOCATE 18, 1
        COLOR 14, 0
        Print "        ★ NEW HIGH SCORE! ★"

        LOCATE 19, 1
        COLOR 15, 0
        Print "        Enter name (3 letters): "

        ' For demo, use a default name
        Dim playerName As String
        playerName = "YOU"

        ScoreBoard.AddScore(playerName, GameScore, GameLevel)
    End If

    LOCATE 22, 1
    COLOR 8, 0
    Print "Press any key to return to menu...";
    COLOR 7, 0

    Dim k As String
    k = ""
    While k = ""
        k = Inkey$()
    Wend
End Sub

' === MAIN PROGRAM ===
Randomize

Dim menuChoice As String
Dim running As Integer
running = 1

While running = 1
    ShowMainMenu()

    menuChoice = ""
    While menuChoice = ""
        menuChoice = Inkey$()
    Wend

    If menuChoice = "1" Then
        InitGame()
        GameLoop()
        GameOverScreen()
    ElseIf menuChoice = "2" Then
        ShowHighScores()
    ElseIf menuChoice = "3" Then
        ShowInstructions()
    ElseIf menuChoice = "q" Or menuChoice = "Q" Then
        running = 0
    End If
Wend

' Exit message
CLS
LOCATE 12, 1
COLOR 11, 0
Print "     Thanks for playing vTRIS!"
LOCATE 13, 1
COLOR 7, 0
Print "     A Viper BASIC Demonstration"
LOCATE 15, 1
