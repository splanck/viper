' main.bas - Entry Point with Interactive REPL
' Part of SQLite Clone - Viper Basic Implementation

AddFile "executor.bas"

'=============================================================================
' MAIN ENTRY POINT
'=============================================================================

SUB Main()
    DIM inputLine AS STRING
    DIM result AS QueryResult
    DIM running AS INTEGER

    PRINT "ViperSQL - SQLite Clone in Viper Basic"
    PRINT "Type SQL commands or 'exit' to quit."
    PRINT ""

    InitDatabase()
    running = 1

    WHILE running = 1
        PRINT "sql> ";
        INPUT inputLine

        IF inputLine = "exit" OR inputLine = "quit" THEN
            running = 0
        ELSEIF inputLine <> "" THEN
            LET result = ExecuteSql(inputLine)
            PRINT result.ToString$()
            PRINT ""
        END IF
    WEND

    PRINT "Goodbye!"
END SUB

' Run main
Main()
