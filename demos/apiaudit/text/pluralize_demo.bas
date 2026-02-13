' pluralize_demo.bas
PRINT "=== Viper.Text.Pluralize Demo ==="
PRINT Viper.Text.Pluralize.Plural("cat")
PRINT Viper.Text.Pluralize.Plural("child")
PRINT Viper.Text.Pluralize.Singular("cats")
PRINT Viper.Text.Pluralize.Count(3, "item")
PRINT Viper.Text.Pluralize.Count(1, "item")
PRINT "done"
END
