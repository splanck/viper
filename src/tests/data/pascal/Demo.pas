{ Viper Pascal program using MyMath unit }
program Demo;
uses MyMath;
var n: Integer;
begin
  n := Square(5);
  WriteLn(IntToStr(n))
end.
