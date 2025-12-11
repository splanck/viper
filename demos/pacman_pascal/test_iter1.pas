{ Pacman Pascal - Iteration 1: Basic Output and Variables }
program TestIter1;

var
    x: Integer;
    y: Integer;
    sum: Integer;
    name: String;

begin
    WriteLn('=== Pacman Pascal Iteration 1 ===');
    WriteLn('Testing basic output and variables...');
    WriteLn('');

    { Test basic variables }
    x := 10;
    y := 20;
    name := 'Pacman';

    WriteLn('Integer x = ', x);
    WriteLn('Integer y = ', y);
    WriteLn('String name = ', name);
    WriteLn('');

    { Test basic math }
    sum := x + y;
    WriteLn('Sum x + y = ', sum);

    { Test IF statement }
    if sum > 25 then
        WriteLn('Sum is greater than 25')
    else
        WriteLn('Sum is not greater than 25');

    WriteLn('');
    WriteLn('=== Iteration 1 Complete ===');
end.
