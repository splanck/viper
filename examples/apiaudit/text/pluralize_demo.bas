' Zanna.Text.Pluralize API Audit - English Noun Pluralization
' Tests all Pluralize functions

PRINT "=== Zanna.Text.Pluralize API Audit ==="

' --- Plural ---
PRINT "--- Plural ---"
PRINT "cat -> "; Zanna.Text.Pluralize.Plural("cat")
PRINT "box -> "; Zanna.Text.Pluralize.Plural("box")
PRINT "city -> "; Zanna.Text.Pluralize.Plural("city")
PRINT "child -> "; Zanna.Text.Pluralize.Plural("child")
PRINT "person -> "; Zanna.Text.Pluralize.Plural("person")
PRINT "mouse -> "; Zanna.Text.Pluralize.Plural("mouse")
PRINT "leaf -> "; Zanna.Text.Pluralize.Plural("leaf")
PRINT "bus -> "; Zanna.Text.Pluralize.Plural("bus")
PRINT "fish -> "; Zanna.Text.Pluralize.Plural("fish")

' --- Singular ---
PRINT "--- Singular ---"
PRINT "cats -> "; Zanna.Text.Pluralize.Singular("cats")
PRINT "boxes -> "; Zanna.Text.Pluralize.Singular("boxes")
PRINT "cities -> "; Zanna.Text.Pluralize.Singular("cities")
PRINT "children -> "; Zanna.Text.Pluralize.Singular("children")
PRINT "people -> "; Zanna.Text.Pluralize.Singular("people")
PRINT "mice -> "; Zanna.Text.Pluralize.Singular("mice")
PRINT "leaves -> "; Zanna.Text.Pluralize.Singular("leaves")
PRINT "buses -> "; Zanna.Text.Pluralize.Singular("buses")

' --- Count ---
PRINT "--- Count ---"
PRINT "0 cat: "; Zanna.Text.Pluralize.Count(0, "cat")
PRINT "1 cat: "; Zanna.Text.Pluralize.Count(1, "cat")
PRINT "2 cat: "; Zanna.Text.Pluralize.Count(2, "cat")
PRINT "5 box: "; Zanna.Text.Pluralize.Count(5, "box")
PRINT "1 child: "; Zanna.Text.Pluralize.Count(1, "child")
PRINT "3 child: "; Zanna.Text.Pluralize.Count(3, "child")
PRINT "100 person: "; Zanna.Text.Pluralize.Count(100, "person")

PRINT "=== Pluralize Demo Complete ==="
END
