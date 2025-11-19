' vTris - Simple Tetris for Viper BASIC
' A playable test version

AddFile "pieces.bas"
AddFile "board.bas"

' Game variables
Dim GameBoard As Board
Dim CurrentPiece As Piece
Dim NextPieceType As Integer
Dim GameScore As Integer
Dim GameOver As Integer
Dim DropCounter As Integer
Dim DropSpeed As Integer

Sub InitGame()
    GameBoard = New Board()
    GameScore = 0
    GameOver = 0
    DropSpeed = 30
    DropCounter = 0
    NextPieceType = Int(Rnd() * 7)
    SpawnPiece()
End Sub

Sub SpawnPiece()
    CurrentPiece = New Piece(NextPieceType)
    CurrentPiece.PosX = 4
    CurrentPiece.PosY = 0
    
    ' Check if can spawn (game over if not)
    If GameBoard.CanPlace(CurrentPiece) = 0 Then
        GameOver = 1
    End If
    
    ' Generate next piece
    NextPieceType = Int(Rnd() * 7)
End Sub

Sub GameLoop()
    Dim k As String
    
    While GameOver = 0
        ' Get input
        k = Inkey$()
        
        ' Handle input
        If k = "a" Or k = "A" Then
            ' Move left
            CurrentPiece.MoveLeft()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.MoveRight()  ' Undo
            End If
        End If
        
        If k = "d" Or k = "D" Then
            ' Move right
            CurrentPiece.MoveRight()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.MoveLeft()  ' Undo
            End If
        End If
        
        If k = "w" Or k = "W" Then
            ' Rotate
            CurrentPiece.RotateClockwise()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                ' Undo rotation (rotate 3 more times)
                CurrentPiece.RotateClockwise()
                CurrentPiece.RotateClockwise()
                CurrentPiece.RotateClockwise()
            End If
        End If
        
        If k = "s" Or k = "S" Then
            ' Soft drop
            CurrentPiece.MoveDown()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.PosY = CurrentPiece.PosY - 1
                LockPiece()
            End If
        End If
        
        If k = "q" Or k = "Q" Then
            GameOver = 1  ' Quit
        End If
        
        ' Auto drop
        DropCounter = DropCounter + 1
        If DropCounter >= DropSpeed Then
            DropCounter = 0
            CurrentPiece.MoveDown()
            
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                ' Can't move down, lock piece
                CurrentPiece.PosY = CurrentPiece.PosY - 1
                LockPiece()
            End If
        End If
        
        ' Draw everything
        GameBoard.DrawBoard()
        GameBoard.DrawPiece(CurrentPiece)

        ' Draw UI (right side of board)
        LOCATE 3, 26
        Print "vTris v1.0"
        LOCATE 5, 26
        Print "Lines: "; GameBoard.LinesCleared; "  "
        LOCATE 6, 26
        Print "Score: "; GameScore; "  "

        LOCATE 9, 26
        Print "Controls:"
        LOCATE 10, 26
        Print "A/D - Move"
        LOCATE 11, 26
        Print "W - Rotate"
        LOCATE 12, 26
        Print "S - Drop"
        LOCATE 13, 26
        Print "Q - Quit"
        
        ' Small delay
        Sleep 50
    Wend
End Sub

Sub LockPiece()
    Dim lines As Integer
    
    GameBoard.PlacePiece(CurrentPiece)
    lines = GameBoard.CheckLines()
    
    If lines > 0 Then
        GameScore = GameScore + (lines * lines * 100)
    End If
    
    SpawnPiece()
End Sub

' Main program
CLS
Randomize
InitGame()

LOCATE 12, 26
Print "vTris v1.0"
LOCATE 14, 26
Print "Press any key"
LOCATE 15, 26
Print "to start..."

Dim k As String
k = ""
While k = ""
    k = Inkey$()
Wend

CLS
GameLoop()

' Game over screen
CLS
LOCATE 10, 10
Print "GAME OVER!"
LOCATE 12, 10
Print "Final Score: "; GameScore
LOCATE 14, 10
Print "Lines: "; GameBoard.LinesCleared

LOCATE 20, 1
