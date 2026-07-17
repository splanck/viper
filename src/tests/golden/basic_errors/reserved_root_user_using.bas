REM Test E_NS_009: reserved root namespace "Zanna" (USING)
REM User code cannot import Zanna or Zanna.* namespaces

USING Zanna.Text

NAMESPACE MyApplication
  CLASS Program
    DIM version AS I64
  END CLASS
END NAMESPACE

END
