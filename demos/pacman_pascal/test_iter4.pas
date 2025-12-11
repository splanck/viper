{ Pacman Pascal - Iteration 4: Procedures and Functions }
program TestIter4;

var
    GlobalCounter: Integer;

procedure PrintBanner(msg: String);
begin
    WriteLn('===================');
    WriteLn(msg);
    WriteLn('===================');
end;

procedure IncrementCounter;
begin
    GlobalCounter := GlobalCounter + 1;
end;

function Add(a, b: Integer): Integer;
begin
    Result := a + b;
end;

function Multiply(a, b: Integer): Integer;
begin
    Result := a * b;
end;

function Factorial(n: Integer): Integer;
var
    result_val: Integer;
    k: Integer;
begin
    result_val := 1;
    for k := 2 to n do
        result_val := result_val * k;
    Result := result_val;
end;

begin
    WriteLn('=== Pacman Pascal Iteration 4 ===');
    WriteLn('Testing procedures and functions...');
    WriteLn('');

    { Test procedure with parameter }
    PrintBanner('Hello Pascal!');
    WriteLn('');

    { Test global variable modification }
    GlobalCounter := 0;
    WriteLn('GlobalCounter initial: ', GlobalCounter);
    IncrementCounter;
    IncrementCounter;
    IncrementCounter;
    WriteLn('GlobalCounter after 3 increments: ', GlobalCounter);
    WriteLn('');

    { Test functions with return values }
    WriteLn('Add(5, 3) = ', Add(5, 3));
    WriteLn('Multiply(4, 7) = ', Multiply(4, 7));
    WriteLn('');

    { Test function with local variables and loop }
    WriteLn('Factorial(5) = ', Factorial(5));
    WriteLn('Factorial(7) = ', Factorial(7));
    WriteLn('');

    WriteLn('=== Iteration 4 Complete ===');
end.
