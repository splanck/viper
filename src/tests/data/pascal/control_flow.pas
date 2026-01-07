{ Viper Pascal control flow test }
program ControlFlow;
var
  i, sum: Integer;
  done: Boolean;
begin
  sum := 0;
  i := 1;

  { While loop }
  while i <= 10 do
  begin
    sum := sum + i;
    i := i + 1;
  end;

  { For loop }
  for i := 1 to 5 do
  begin
    sum := sum + 1;
  end;

  { If statement }
  if sum > 50 then
    done := True
  else
    done := False;
end.
