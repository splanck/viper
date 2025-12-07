{ Viper Pascal builtin functions test }
program Builtins;
var
  s: String;
  n: Integer;
  x: Real;
begin
  { I/O }
  WriteLn('Hello, World!');

  { String functions }
  s := 'test';
  n := Length(s);
  s := IntToStr(42);

  { Math functions }
  x := Sqrt(16.0);
  x := Sin(0.5);
  x := Cos(0.5);
  n := Trunc(3.14);
  n := Round(2.5);
  n := Abs(-5);

  { Ordinal functions }
  n := Ord(65);
  s := Chr(65);
  n := Pred(10);
  n := Succ(10);

  { Random }
  Randomize();
  x := Random();
end.
