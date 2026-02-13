' buttongroup_demo.bas
PRINT "=== Viper.Game.ButtonGroup Demo ==="
DIM bg AS OBJECT
bg = NEW Viper.Game.ButtonGroup()
PRINT bg.Add(0)
PRINT bg.Add(1)
PRINT bg.Add(2)
PRINT bg.Count
PRINT bg.Select(1)
PRINT bg.Selected
PRINT bg.HasSelection
PRINT bg.IsSelected(1)
PRINT bg.Has(2)
bg.ClearSelection()
PRINT bg.HasSelection
PRINT "done"
END
