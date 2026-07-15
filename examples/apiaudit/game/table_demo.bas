' API Audit: Viper.Game.UI.HudTable and TableClickResult (BASIC)
PRINT "=== API Audit: Viper.Game.UI.HudTable ==="

DIM table AS OBJECT
table = Viper.Game.UI.HudTable.New(0, 0, 220, 96)
table.AddColumn("Name", 100, 0)
table.AddColumn("Score", 80, 0)
table.SetColumnSortable(1, 1, 1)

DIM rowA AS INTEGER
rowA = table.AddRow()
DIM rowB AS INTEGER
rowB = table.AddRow()
table.SetCell(rowA, 0, "Alice")
table.SetCell(rowA, 1, "10")
table.SetCell(rowB, 0, "Bob")
table.SetCell(rowB, 1, "30")

PRINT "--- Header click result ---"
DIM headerClick AS OBJECT
headerClick = table.HandleClick(120, 4)
PRINT headerClick.IsHeader
PRINT headerClick.IsRow
DIM headerColumn AS OBJECT
headerColumn = headerClick.ColumnOption()
PRINT headerColumn.IsSome
PRINT headerColumn.UnwrapI64()

PRINT "--- Row click result ---"
DIM rowClick AS OBJECT
rowClick = table.HandleClick(10, 40)
PRINT rowClick.IsRow
PRINT rowClick.IsHeader
DIM selectedRow AS OBJECT
selectedRow = rowClick.RowOption()
PRINT selectedRow.IsSome
PRINT selectedRow.UnwrapI64()

PRINT "--- Miss click result ---"
DIM missClick AS OBJECT
missClick = table.HandleClick(400, 400)
PRINT missClick.IsNone
PRINT missClick.RowOption().IsNone
PRINT missClick.ColumnOption().IsNone

PRINT table.RowCount
PRINT "=== Table Audit Complete ==="
END
