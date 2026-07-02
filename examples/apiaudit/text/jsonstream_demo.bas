' Viper.Text.JsonStream API Audit - Streaming JSON Parser
' Tests all JsonStream functions

PRINT "=== Viper.Text.JsonStream API Audit ==="

' --- New / NextResult / TokenType ---
PRINT "--- New / NextResult / TokenType ---"
DIM stream AS OBJECT
stream = Viper.Text.JsonStream.New("{""name"":""Alice"",""age"":30,""active"":true}")

DIM i AS INTEGER
DIM nextToken AS OBJECT
i = 0
DO WHILE i < 20
    IF NOT Viper.Text.JsonStream.HasNext(stream) THEN
        i = 100
    ELSE
        nextToken = Viper.Text.JsonStream.NextResult(stream)
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
s2 = Viper.Text.JsonStream.New("{""greeting"":""hello""}")
Viper.Text.JsonStream.Next(s2)
Viper.Text.JsonStream.Next(s2)
PRINT "Key: "; Viper.Text.JsonStream.StringValue(s2)
Viper.Text.JsonStream.Next(s2)
PRINT "Value: "; Viper.Text.JsonStream.StringValue(s2)

' --- NumberValue ---
PRINT "--- NumberValue ---"
DIM s3 AS OBJECT
s3 = Viper.Text.JsonStream.New("{""value"":42.5}")
Viper.Text.JsonStream.Next(s3)
Viper.Text.JsonStream.Next(s3)
Viper.Text.JsonStream.Next(s3)
PRINT "Number: "; Viper.Text.JsonStream.NumberValue(s3)

' --- BoolValue ---
PRINT "--- BoolValue ---"
DIM s4 AS OBJECT
s4 = Viper.Text.JsonStream.New("{""flag"":true}")
Viper.Text.JsonStream.Next(s4)
Viper.Text.JsonStream.Next(s4)
Viper.Text.JsonStream.Next(s4)
PRINT "Bool: "; Viper.Text.JsonStream.BoolValue(s4)

' --- Depth ---
PRINT "--- Depth ---"
DIM s5 AS OBJECT
s5 = Viper.Text.JsonStream.New("{""nested"":{""deep"":1}}")
Viper.Text.JsonStream.Next(s5)
PRINT "Depth at {: "; Viper.Text.JsonStream.Depth(s5)
Viper.Text.JsonStream.Next(s5)
Viper.Text.JsonStream.Next(s5)
PRINT "Depth at nested {: "; Viper.Text.JsonStream.Depth(s5)

' --- Skip ---
PRINT "--- Skip ---"
DIM s6 AS OBJECT
s6 = Viper.Text.JsonStream.New("{""skip"":[1,2,3],""keep"":""yes""}")
Viper.Text.JsonStream.Next(s6)
Viper.Text.JsonStream.Next(s6)
Viper.Text.JsonStream.Skip(s6)
Viper.Text.JsonStream.Next(s6)
PRINT "After skip key: "; Viper.Text.JsonStream.StringValue(s6)
Viper.Text.JsonStream.Next(s6)
PRINT "After skip val: "; Viper.Text.JsonStream.StringValue(s6)

' --- HasNext ---
PRINT "--- HasNext ---"
DIM s7 AS OBJECT
s7 = Viper.Text.JsonStream.New("42")
PRINT "HasNext before: "; Viper.Text.JsonStream.HasNext(s7)
Viper.Text.JsonStream.Next(s7)
PRINT "HasNext after: "; Viper.Text.JsonStream.HasNext(s7)

' --- NextResult / Error compatibility ---
PRINT "--- NextResult / Error compatibility ---"
DIM s8 AS OBJECT
s8 = Viper.Text.JsonStream.New("not json")
DIM bad AS OBJECT
bad = Viper.Text.JsonStream.NextResult(s8)
PRINT "Bad NextResult IsErr: "; bad.IsErr
PRINT "Bad NextResult Err: "; bad.UnwrapErrStr()
PRINT "Error: "; Viper.Text.JsonStream.Error(s8)

PRINT "=== JsonStream Demo Complete ==="
END
