program Test;
type
  TInner = class
  public
    Val: Integer;
    procedure IncVal;
  end;

procedure TInner.IncVal;
begin
  Inc(Val)
end;

begin
end.
