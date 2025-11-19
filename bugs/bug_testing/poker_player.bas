REM poker_player.bas - Player class

Class Player
    Public name AS String
    Public chips AS Integer
    Public currentBet AS Integer
    Public folded AS Boolean
    Public hand AS Hand

    Sub New(playerName AS String, startChips AS Integer)
        LET ME.name = playerName
        LET ME.chips = startChips
        LET ME.currentBet = 0
        LET ME.folded = FALSE
        LET ME.hand = NEW Hand()
    End Sub

    Sub ResetForNewHand()
        LET ME.currentBet = 0
        LET ME.folded = FALSE
        LET ME.hand = NEW Hand()
    End Sub

    Function CanBet(amount AS Integer) AS Boolean
        LET CanBet = ME.chips >= amount
    End Function

    Sub PlaceBet(amount AS Integer)
        IF ME.CanBet(amount) THEN
            LET ME.chips = ME.chips - amount
            LET ME.currentBet = ME.currentBet + amount
        END IF
    End Sub

    Sub Fold()
        LET ME.folded = TRUE
    End Sub

    Sub WinPot(amount AS Integer)
        LET ME.chips = ME.chips + amount
    End Sub

    Sub ShowStatus()
        PRINT ME.name; " - Chips: "; ME.chips; " - Bet: "; ME.currentBet
        IF ME.folded THEN
            PRINT "  (FOLDED)"
        ELSE
            ME.hand.ShowHand()
        END IF
    End Sub
End Class
