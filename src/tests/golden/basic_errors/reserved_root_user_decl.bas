REM Test E_NS_009: reserved root namespace "Zanna" (declaration)
REM User code cannot declare NAMESPACE Zanna or Zanna.*

NAMESPACE Zanna.MyLibrary
  CLASS Helper
    DIM value AS I64
  END CLASS
END NAMESPACE

END
