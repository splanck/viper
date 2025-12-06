' Test 28: Test scoreboard initialization and display

AddFile "../../demos/vTris/scoreboard.bas"

Print "Testing scoreboard initialization..."
Print ""

Dim sb As Scoreboard
sb = New Scoreboard()

Print "✓ Scoreboard initialized successfully"
Print ""

' Test that we can access the count
Print "Default high score count: "; sb.Count
Print ""

' Test drawing scoreboard (should not crash)
Print "Drawing scoreboard..."
Print ""
sb.DrawScoreboard(1)

Print ""
Print "✓ Scoreboard display completed successfully"
Print ""
Print "If you can see this, the bug is fixed!"
