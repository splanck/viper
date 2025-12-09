program Test;
type
  TAnimal = class
  public
    procedure Speak; virtual; abstract;
  end;
  TDog = class(TAnimal)
  public
    procedure Speak; override;
  end;
procedure TDog.Speak;
begin
end;
var d: TDog;
begin
  d := TDog.Create;
  d.Speak
end.
