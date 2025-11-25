' Bug test: LOCATE with out-of-range values - narrowing cast
Dim row As Integer
Dim col As Integer

' Test with values that would overflow byte range
row = 300
col = 400
Print "Attempting LOCATE "; row; ", "; col
LOCATE row, col
Print "X"
