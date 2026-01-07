REM Test E_NS_009: reserved root namespace "Viper" (declaration at root)
REM User code cannot declare NAMESPACE Viper or Viper.*

NAMESPACE Viper.Tools
  SUB Hello()
  END SUB
END NAMESPACE

