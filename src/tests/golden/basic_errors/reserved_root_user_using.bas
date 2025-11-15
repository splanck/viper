REM Test E_NS_009: reserved root namespace "Viper" (USING)
REM User code cannot import Viper or Viper.* namespaces

USING Viper.System.Text

NAMESPACE MyApplication
  CLASS Program
    DIM version AS I64
  END CLASS
END NAMESPACE

END
