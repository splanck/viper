{ Viper Pascal math unit test }
unit MyMath;
interface
  const Tau = 6.28318;
  function Square(x: Integer): Integer;
implementation
  function Square(x: Integer): Integer;
  begin
    Square := x * x
  end;
end.
