program Test;
type
  TAnimal = class
  public
    procedure Speak; virtual; abstract;
  end;
var a: TAnimal;
begin
  a := TAnimal.Create
end.
