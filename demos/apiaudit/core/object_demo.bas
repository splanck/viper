' object_demo.bas
PRINT "=== Viper.Core.Object Demo ==="
DIM list AS OBJECT
list = NEW Viper.Collections.List()
PRINT Viper.Core.Object.ToString(list)
PRINT Viper.Core.Object.TypeName(list)
PRINT Viper.Core.Object.TypeId(list)
PRINT Viper.Core.Object.IsNull(list)
PRINT "done"
END
