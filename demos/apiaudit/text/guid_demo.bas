' Viper.Text.Uuid API Audit - UUID Generation and Validation
' Tests all Uuid functions

PRINT "=== Viper.Text.Uuid API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM id1 AS STRING
DIM id2 AS STRING
id1 = Viper.Text.Uuid.New()
id2 = Viper.Text.Uuid.New()
PRINT "UUID 1: "; id1
PRINT "UUID 2: "; id2

' --- get_Empty ---
PRINT "--- get_Empty ---"
DIM empty AS STRING
empty = Viper.Text.Uuid.get_Empty()
PRINT "Empty UUID: "; empty

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "IsValid(id1): "; Viper.Text.Uuid.IsValid(id1)
PRINT "IsValid(empty): "; Viper.Text.Uuid.IsValid(empty)
PRINT "IsValid('not-a-uuid'): "; Viper.Text.Uuid.IsValid("not-a-uuid")
PRINT "IsValid(rfc): "; Viper.Text.Uuid.IsValid("550e8400-e29b-41d4-a716-446655440000")

' --- ToBytes / FromBytes ---
PRINT "--- ToBytes / FromBytes ---"
DIM testId AS STRING
testId = Viper.Text.Uuid.New()
PRINT "Original: "; testId
DIM bytes AS OBJECT
bytes = Viper.Text.Uuid.ToBytes(testId)
DIM restored AS STRING
restored = Viper.Text.Uuid.FromBytes(bytes)
PRINT "Restored: "; restored

PRINT "=== Uuid Demo Complete ==="
END
