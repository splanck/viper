{ Enum and case statement demonstration }
program EnumCase;

type
  Color = (Red, Green, Blue);

var
  c: Color;

begin
  WriteLn('Enum and Case Statement Demo');
  WriteLn;

  { Test each enum value with case }
  c := Red;
  case c of
    Red: WriteLn('Color is Red');
    Green: WriteLn('Color is Green');
    Blue: WriteLn('Color is Blue')
  end;

  c := Green;
  case c of
    Red: WriteLn('Color is Red');
    Green: WriteLn('Color is Green');
    Blue: WriteLn('Color is Blue')
  end;

  c := Blue;
  case c of
    Red: WriteLn('Color is Red');
    Green: WriteLn('Color is Green');
    Blue: WriteLn('Color is Blue')
  end;

  WriteLn;
  WriteLn('Integer case with multiple labels:');

  { Test integer case with multiple labels and else }
  case 5 of
    1, 2, 3: WriteLn('Value is 1, 2, or 3');
    4, 5: WriteLn('Value is 4 or 5')
  else
    WriteLn('Value is something else')
  end
end.
