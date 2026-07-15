' Viper.Data.JsonStream API Audit - Streaming JSON Parser
' Tests all JsonStream functions

PRINT "=== Viper.Data.JsonStream API Audit ==="

' --- New / NextResult / TokenType ---
PRINT "--- New / NextResult / TokenType ---"
DIM stream AS OBJECT
stream = Viper.Data.JsonStream.New("{""name"":""Alice"",""age"":30,""active"":true}")

DIM i AS INTEGER
DIM nextToken AS OBJECT
i = 0
DO WHILE i < 20
    IF NOT Viper.Data.JsonStream.HasNext(stream) THEN
        i = 100
    ELSE
        nextToken = Viper.Data.JsonStream.NextResult(stream)
        IF nextToken.IsErr THEN
            PRINT "Token error: "; nextToken.UnwrapErrStr()
            i = 100
        ELSE
            PRINT "Token type: "; nextToken.UnwrapI64()
        END IF
        i = i + 1
    END IF
LOOP

' --- StringValue ---
PRINT "--- StringValue ---"
DIM s2 AS OBJECT
s2 = Viper.Data.JsonStream.New("{""greeting"":""hello""}")
Viper.Data.JsonStream.Next(s2)
Viper.Data.JsonStream.Next(s2)
PRINT "Key: "; Viper.Data.JsonStream.StringValue(s2)
Viper.Data.JsonStream.Next(s2)
PRINT "Value: "; Viper.Data.JsonStream.StringValue(s2)

' --- NumberValue ---
PRINT "--- NumberValue ---"
DIM s3 AS OBJECT
s3 = Viper.Data.JsonStream.New("{""value"":42.5}")
Viper.Data.JsonStream.Next(s3)
Viper.Data.JsonStream.Next(s3)
Viper.Data.JsonStream.Next(s3)
PRINT "Number: "; Viper.Data.JsonStream.NumberValue(s3)

' --- BoolValue ---
PRINT "--- BoolValue ---"
DIM s4 AS OBJECT
s4 = Viper.Data.JsonStream.New("{""flag"":true}")
Viper.Data.JsonStream.Next(s4)
Viper.Data.JsonStream.Next(s4)
Viper.Data.JsonStream.Next(s4)
PRINT "Bool: "; Viper.Data.JsonStream.BoolValue(s4)

' --- Depth ---
PRINT "--- Depth ---"
DIM s5 AS OBJECT
s5 = Viper.Data.JsonStream.New("{""nested"":{""deep"":1}}")
Viper.Data.JsonStream.Next(s5)
PRINT "Depth at {: "; Viper.Data.JsonStream.Depth(s5)
Viper.Data.JsonStream.Next(s5)
Viper.Data.JsonStream.Next(s5)
PRINT "Depth at nested {: "; Viper.Data.JsonStream.Depth(s5)

' --- Skip ---
PRINT "--- Skip ---"
DIM s6 AS OBJECT
s6 = Viper.Data.JsonStream.New("{""skip"":[1,2,3],""keep"":""yes""}")
Viper.Data.JsonStream.Next(s6)
Viper.Data.JsonStream.Next(s6)
Viper.Data.JsonStream.Skip(s6)
Viper.Data.JsonStream.Next(s6)
PRINT "After skip key: "; Viper.Data.JsonStream.StringValue(s6)
Viper.Data.JsonStream.Next(s6)
PRINT "After skip val: "; Viper.Data.JsonStream.StringValue(s6)

' --- HasNext ---
PRINT "--- HasNext ---"
DIM s7 AS OBJECT
s7 = Viper.Data.JsonStream.New("42")
PRINT "HasNext before: "; Viper.Data.JsonStream.HasNext(s7)
Viper.Data.JsonStream.Next(s7)
PRINT "HasNext after: "; Viper.Data.JsonStream.HasNext(s7)

' --- NextResult / Error compatibility ---
PRINT "--- NextResult / Error compatibility ---"
DIM s8 AS OBJECT
s8 = Viper.Data.JsonStream.New("not json")
DIM bad AS OBJECT
bad = Viper.Data.JsonStream.NextResult(s8)
PRINT "Bad NextResult IsErr: "; bad.IsErr
PRINT "Bad NextResult Err: "; bad.UnwrapErrStr()
PRINT "Error: "; Viper.Data.JsonStream.Error(s8)

PRINT "=== JsonStream Demo Complete ==="
END
