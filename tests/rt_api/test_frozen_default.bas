' test_frozen_default.bas — FrozenSet, FrozenMap, DefaultMap
' NOTE: FrozenSet.New, FrozenMap.New not recognized by BASIC (BUG-009)
' NOTE: DefaultMap.New(obj) — cannot assign string to OBJECT in BASIC (BUG-023)

PRINT "all skipped: FrozenSet/FrozenMap/DefaultMap not usable in BASIC"
PRINT "FrozenSet/FrozenMap: unknown procedure (BUG-009)"
PRINT "DefaultMap: str-to-obj coercion fails (BUG-023)"
PRINT "done"
END
