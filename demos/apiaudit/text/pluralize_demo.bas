' Viper.Text.Pluralize API Audit - English Noun Pluralization
' Tests all Pluralize functions

PRINT "=== Viper.Text.Pluralize API Audit ==="

' --- Plural ---
PRINT "--- Plural ---"
PRINT "cat -> "; Viper.Text.Pluralize.Plural("cat")
PRINT "box -> "; Viper.Text.Pluralize.Plural("box")
PRINT "city -> "; Viper.Text.Pluralize.Plural("city")
PRINT "child -> "; Viper.Text.Pluralize.Plural("child")
PRINT "person -> "; Viper.Text.Pluralize.Plural("person")
PRINT "mouse -> "; Viper.Text.Pluralize.Plural("mouse")
PRINT "leaf -> "; Viper.Text.Pluralize.Plural("leaf")
PRINT "bus -> "; Viper.Text.Pluralize.Plural("bus")
PRINT "fish -> "; Viper.Text.Pluralize.Plural("fish")

' --- Singular ---
PRINT "--- Singular ---"
PRINT "cats -> "; Viper.Text.Pluralize.Singular("cats")
PRINT "boxes -> "; Viper.Text.Pluralize.Singular("boxes")
PRINT "cities -> "; Viper.Text.Pluralize.Singular("cities")
PRINT "children -> "; Viper.Text.Pluralize.Singular("children")
PRINT "people -> "; Viper.Text.Pluralize.Singular("people")
PRINT "mice -> "; Viper.Text.Pluralize.Singular("mice")
PRINT "leaves -> "; Viper.Text.Pluralize.Singular("leaves")
PRINT "buses -> "; Viper.Text.Pluralize.Singular("buses")

' --- Count ---
PRINT "--- Count ---"
PRINT "0 cat: "; Viper.Text.Pluralize.Count(0, "cat")
PRINT "1 cat: "; Viper.Text.Pluralize.Count(1, "cat")
PRINT "2 cat: "; Viper.Text.Pluralize.Count(2, "cat")
PRINT "5 box: "; Viper.Text.Pluralize.Count(5, "box")
PRINT "1 child: "; Viper.Text.Pluralize.Count(1, "child")
PRINT "3 child: "; Viper.Text.Pluralize.Count(3, "child")
PRINT "100 person: "; Viper.Text.Pluralize.Count(100, "person")

PRINT "=== Pluralize Demo Complete ==="
END
