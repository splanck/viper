program Test;
type
  TInner = class
  public
    procedure IncVal;
  end;
  TOuter = class
  private
    Inner: TInner;
  public
    procedure Bump;
  end;

procedure TInner.IncVal;
begin
end;

procedure TOuter.Bump;
begin
  Inner.IncVal
end;

var o: TOuter;
begin
  o.Bump
end.
