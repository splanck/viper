' Test 19: Visual showcase of vTris demo features

AddFile "../../demos/vTris/scoreboard.bas"

Print "vTRIS v2.0 - Visual Showcase"
Print ""
Print "Displaying high scores with colors..."
Print ""

Sleep 1000

CLS

' Create and display scoreboard
Dim sb As Scoreboard
sb = New Scoreboard()

' Add a few more scores for demo
sb.AddScore("YOU", 45000, 9)
sb.AddScore("WIN", 30000, 7)

' Draw the scoreboard
sb.DrawScoreboard(3)

' Add instructions below
LOCATE 18, 1
COLOR 7, 0
Print ""
Print ""
Print "    This is what players see in the High Scores menu!"
Print ""
Print "    Features:"
COLOR 11, 0
Print "      • Top 10 leaderboard"
COLOR 10, 0
Print "      • Colored rankings (1st=Cyan, 2nd=Green, 3rd=Blue)"
COLOR 14, 0
Print "      • Score and level display"
COLOR 7, 0
Print "      • Professional presentation"
Print ""
