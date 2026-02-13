' API Audit: Viper.Game.ButtonGroup (BASIC)
PRINT "=== API Audit: Viper.Game.ButtonGroup ==="
DIM bg AS OBJECT = Viper.Game.ButtonGroup.New()
bg.Add(0)
bg.Add(1)
bg.Add(2)
bg.Add(3)
bg.Add(4)
PRINT bg.Count
PRINT bg.Selected
PRINT bg.HasSelection
bg.Select(2)
PRINT bg.Selected
PRINT bg.IsSelected(2)
PRINT bg.IsSelected(0)
bg.ClearSelection()
PRINT bg.HasSelection
PRINT bg.Has(3)
bg.Remove(4)
PRINT bg.Count
PRINT "=== ButtonGroup Audit Complete ==="
END
