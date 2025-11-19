' Test 05: AddFile keyword
' Testing: including external BASIC files

AddFile "shapes_lib.bas"

' Use the class from the included file
Dim shape As ShapeData
shape = New ShapeData(1, "I-Piece")

Print "Testing AddFile:"
Print shape.GetInfo()

' Create multiple shapes
Dim shape2 As ShapeData
shape2 = New ShapeData(2, "T-Piece")
Print shape2.GetInfo()
