{ Pacman Pascal - Simple array test }
program TestArraySimple;

var
    Board: array[10] of Integer;
    i: Integer;

begin
    WriteLn('=== Simple Array Test ===');

    { Initialize array }
    WriteLn('1. Initializing array...');
    for i := 0 to 9 do
        Board[i] := i * 2;

    WriteLn('2. Reading array...');
    for i := 0 to 9 do
        WriteLn('  Board[', i, '] = ', Board[i]);

    WriteLn('');
    WriteLn('=== Test Complete ===');
end.
