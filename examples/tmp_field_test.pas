program Test;
type
  TInner = class
  public
    Val: Integer;
    procedure IncVal;
  end;

  TOuter = class
  private
    Inner: TInner;
  public
    constructor Create;
    procedure Bump;
  end;

constructor TOuter.Create;
begin
  Inner := TInner.Create;
  Inner.Val := 1
end;

procedure TInner.IncVal;
begin
  Inc(Val)
end;

procedure TOuter.Bump;
var tmp: TInner;
begin
  Inner.IncVal;
  tmp := Inner;
  tmp.IncVal;
  self.Inner.IncVal
end;

begin
end.
