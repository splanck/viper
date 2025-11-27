REM ====================================================================
REM MONOPOLY - Trading System
REM Property trading between players
REM ====================================================================

REM Trade state variables
DIM tradeFromPlayer AS INTEGER
DIM tradeToPlayer AS INTEGER
DIM tradePropsFrom AS Viper.Collections.List
DIM tradePropsTo AS Viper.Collections.List
DIM tradeMoneyFrom AS INTEGER
DIM tradeMoneyTo AS INTEGER

REM ====================================================================
REM Trade system disabled due to compiler type inference bugs
REM ====================================================================

REM Stub functions - trading not functional
SUB InitTrade(fromPlayer AS INTEGER, toPlayer AS INTEGER)
    REM Disabled
END SUB

SUB AddPropertyFromOffer(propIdx AS INTEGER)
    REM Disabled
END SUB

SUB AddPropertyToOffer(propIdx AS INTEGER)
    REM Disabled
END SUB

SUB SetTradeMoney(fromMoney AS INTEGER, toMoney AS INTEGER)
    REM Disabled
END SUB

SUB ExecuteTrade()
    REM Disabled
END SUB

REM ====================================================================
REM Show trade interface for human player
REM ====================================================================
FUNCTION ShowTradeInterface(playerIdx AS INTEGER) AS INTEGER
    REM Trading disabled due to compiler bugs with type inference
    REM See BUG-OOP-016 in /bugs/oop_bugs.md
    ShowMessage("Trading is currently disabled.")
    SLEEP 1500
    ShowTradeInterface = 0
END FUNCTION

REM NOTE: Full trade interface disabled due to compiler type inference bugs (BUG-OOP-016)

REM ====================================================================
REM Run auction for a property
REM Simplified due to compiler bugs with nested IF (BUG-OOP-012)
REM ====================================================================
FUNCTION RunAuction(propIdx AS INTEGER) AS INTEGER
    REM Auctions disabled - return -1 (no winner)
    ShowMessage("Auctions are currently disabled.")
    SLEEP 1000
    RunAuction = -1
END FUNCTION

REM RunAuction full implementation disabled due to compiler bugs

