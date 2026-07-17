' Zanna.Text.Uuid API Audit - UUID Generation and Validation
' Tests all Uuid functions

PRINT "=== Zanna.Text.Uuid API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM id1 AS STRING
DIM id2 AS STRING
id1 = Zanna.Text.Uuid.Generate()
id2 = Zanna.Text.Uuid.Generate()
PRINT "UUID 1: "; id1
PRINT "UUID 2: "; id2

' --- get_Empty ---
PRINT "--- get_Empty ---"
DIM empty AS STRING
empty = Zanna.Text.Uuid.get_Empty()
PRINT "Empty UUID: "; empty

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "IsValid(id1): "; Zanna.Text.Uuid.IsValid(id1)
PRINT "IsValid(empty): "; Zanna.Text.Uuid.IsValid(empty)
PRINT "IsValid('not-a-uuid'): "; Zanna.Text.Uuid.IsValid("not-a-uuid")
PRINT "IsValid(rfc): "; Zanna.Text.Uuid.IsValid("550e8400-e29b-41d4-a716-446655440000")

' --- ToBytes / FromBytes ---
PRINT "--- ToBytes / FromBytes ---"
DIM testId AS STRING
testId = Zanna.Text.Uuid.Generate()
PRINT "Original: "; testId
DIM bytes AS OBJECT
bytes = Zanna.Text.Uuid.ToBytes(testId)
DIM restored AS STRING
restored = Zanna.Text.Uuid.FromBytes(bytes)
PRINT "Restored: "; restored

PRINT "=== Uuid Demo Complete ==="
END
