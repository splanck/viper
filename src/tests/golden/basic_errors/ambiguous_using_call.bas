REM BASIC: Ambiguous unqualified call from two USING imports
USING Viper.Terminal
USING My.Console

NAMESPACE My.Console
  SUB PrintI64(x AS INTEGER)
  END SUB
END NAMESPACE

PrintI64(1)

