{ Pacman Pascal - Iteration 3: FOR loops and arrays }
program TestIter3;

var
    i: Integer;
    j: Integer;
    k: Integer;
    sum: Integer;
    Board: array[100] of Integer;
    MazeRow: array[30] of Integer;

begin
    WriteLn('=== Pacman Pascal Iteration 3 ===');
    WriteLn('Testing FOR loops and arrays...');
    WriteLn('');

    { Test basic FOR loop }
    WriteLn('1. Basic FOR loop (1 to 5):');
    for i := 1 to 5 do
        WriteLn('  i = ', i);

    WriteLn('');

    { Test FOR loop with calculation }
    WriteLn('2. FOR loop with sum (1 to 10):');
    sum := 0;
    for i := 1 to 10 do
        sum := sum + i;
    WriteLn('  Sum = ', sum);

    WriteLn('');

    { Test nested FOR loops }
    WriteLn('3. Nested FOR loops (3x3 grid):');
    for i := 1 to 3 do
    begin
        for j := 1 to 3 do
            Write(i * 10 + j, ' ');
        WriteLn('');
    end;

    WriteLn('');

    { Test array initialization }
    WriteLn('4. Array initialization:');
    for i := 0 to 9 do
        Board[i] := i * 2;

    WriteLn('  Board[0..9] = ');
    for i := 0 to 9 do
        Write(Board[i], ' ');
    WriteLn('');

    WriteLn('');

    { Test 2D array simulation (maze row) }
    WriteLn('5. Maze row simulation (28 cells):');
    { 0=empty, 1=wall, 2=dot }
    for i := 0 to 27 do
    begin
        if (i = 0) or (i = 27) then
            MazeRow[i] := 1   { Wall at edges }
        else if (i mod 3) = 0 then
            MazeRow[i] := 1   { Wall every 3rd cell }
        else
            MazeRow[i] := 2;  { Dot }
    end;

    Write('  ');
    for i := 0 to 27 do
    begin
        if MazeRow[i] = 1 then
            Write('#')
        else if MazeRow[i] = 2 then
            Write('.')
        else
            Write(' ');
    end;
    WriteLn('');

    WriteLn('');

    { Test WHILE loop }
    WriteLn('6. WHILE loop (count to 5):');
    k := 1;
    while k <= 5 do
    begin
        WriteLn('  k = ', k);
        k := k + 1;
    end;

    WriteLn('');
    WriteLn('=== Iteration 3 Complete ===');
end.
