' uuid_demo.bas
PRINT "=== Viper.Text.Uuid Demo ==="
DIM u AS STRING
u = Viper.Text.Uuid.New()
PRINT LEN(u)
PRINT Viper.Text.Uuid.IsValid(u)
PRINT Viper.Text.Uuid.IsValid("not-a-uuid")
PRINT Viper.Text.Uuid.Empty
PRINT "done"
END
