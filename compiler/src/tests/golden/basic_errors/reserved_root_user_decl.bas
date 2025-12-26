REM Test E_NS_009: reserved root namespace "Viper" (declaration)
REM User code cannot declare NAMESPACE Viper or Viper.*

NAMESPACE Viper.MyLibrary
  CLASS Helper
    DIM value AS I64
  END CLASS
END NAMESPACE

END
