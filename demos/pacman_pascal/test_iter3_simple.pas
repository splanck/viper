{ Pacman Pascal - Iteration 3 Simple: Just FOR loops }
program TestIter3Simple;

var
    i: Integer;
    sum: Integer;

begin
    WriteLn('=== Simple FOR loop test ===');

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
    WriteLn('=== Test Complete ===');
end.
