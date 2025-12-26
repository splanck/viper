{ OOP Shapes Example - Demonstrates Pascal OOP features }
{ Features: Classes, interfaces, inheritance, virtual methods, properties, RTTI }

program OOPShapes;

type
  { Interface for drawable objects }
  IDrawable = interface
    procedure Draw;
  end;

  { Interface for movable objects }
  IMovable = interface
    procedure Move(dx: Integer; dy: Integer);
  end;

  { Abstract base class for all shapes }
  TShape = class(IDrawable)
  private
    FX: Integer;
    FY: Integer;
    FName: String;
  public
    constructor Create(x: Integer; y: Integer; name: String);

    { Properties with field accessors }
    property X: Integer read FX write FX;
    property Y: Integer read FY write FY;
    property Name: String read FName;

    { Abstract method - must be implemented by subclasses }
    procedure Draw; virtual; abstract;

    { Virtual method with default implementation }
    function GetDescription: String; virtual;
  end;

  { Concrete rectangle class }
  TRectangle = class(TShape, IMovable)
  private
    FWidth: Integer;
    FHeight: Integer;
  public
    constructor Create(x: Integer; y: Integer; w: Integer; h: Integer);

    property Width: Integer read FWidth write FWidth;
    property Height: Integer read FHeight write FHeight;

    procedure Draw; override;
    procedure Move(dx: Integer; dy: Integer);
    function GetArea: Integer;
    function GetDescription: String; override;
  end;

  { Concrete circle class }
  TCircle = class(TShape, IMovable)
  private
    FRadius: Integer;
  public
    constructor Create(x: Integer; y: Integer; r: Integer);

    property Radius: Integer read FRadius write FRadius;

    procedure Draw; override;
    procedure Move(dx: Integer; dy: Integer);
    function GetDescription: String; override;
  end;

{ TShape implementation }
constructor TShape.Create(x: Integer; y: Integer; name: String);
begin
  FX := x;
  FY := y;
  FName := name
end;

function TShape.GetDescription: String;
begin
  Result := FName
end;

{ TRectangle implementation }
constructor TRectangle.Create(x: Integer; y: Integer; w: Integer; h: Integer);
begin
  inherited Create(x, y, 'Rectangle');
  FWidth := w;
  FHeight := h
end;

procedure TRectangle.Draw;
begin
  WriteLn('Drawing rectangle at (', X, ',', Y, ') size ', Width, 'x', Height)
end;

procedure TRectangle.Move(dx: Integer; dy: Integer);
begin
  X := X + dx;
  Y := Y + dy
end;

function TRectangle.GetArea: Integer;
begin
  Result := Width * Height
end;

function TRectangle.GetDescription: String;
begin
  Result := inherited GetDescription + ' [' + IntToStr(Width) + 'x' + IntToStr(Height) + ']'
end;

{ TCircle implementation }
constructor TCircle.Create(x: Integer; y: Integer; r: Integer);
begin
  inherited Create(x, y, 'Circle');
  FRadius := r
end;

procedure TCircle.Draw;
begin
  WriteLn('Drawing circle at (', X, ',', Y, ') radius ', Radius)
end;

procedure TCircle.Move(dx: Integer; dy: Integer);
begin
  X := X + dx;
  Y := Y + dy
end;

function TCircle.GetDescription: String;
begin
  Result := inherited GetDescription + ' [r=' + IntToStr(Radius) + ']'
end;

{ Helper procedure to draw any IDrawable }
procedure DrawShape(d: IDrawable);
begin
  d.Draw
end;

{ Helper procedure to move any IMovable }
procedure MoveShape(m: IMovable; dx: Integer; dy: Integer);
begin
  m.Move(dx, dy)
end;

{ Demonstrate RTTI with IS and AS operators }
procedure DescribeShape(s: TShape);
begin
  WriteLn('Shape: ', s.GetDescription);

  if s is TRectangle then
  begin
    WriteLn('  Area: ', (s as TRectangle).GetArea)
  end;

  if s is TCircle then
  begin
    WriteLn('  This is a circle with radius ', (s as TCircle).Radius)
  end
end;

var
  rect: TRectangle;
  circle: TCircle;
  shapes: array[0..1] of TShape;
  i: Integer;

begin
  WriteLn('=== Pascal OOP Shapes Demo ===');
  WriteLn;

  { Create shapes }
  rect := TRectangle.Create(10, 20, 100, 50);
  circle := TCircle.Create(50, 50, 25);

  { Store in base class array for polymorphism }
  shapes[0] := rect;
  shapes[1] := circle;

  { Demonstrate virtual dispatch through base class }
  WriteLn('--- Drawing all shapes ---');
  for i := 0 to 1 do
  begin
    shapes[i].Draw
  end;
  WriteLn;

  { Demonstrate interface polymorphism }
  WriteLn('--- Drawing via IDrawable interface ---');
  DrawShape(rect);
  DrawShape(circle);
  WriteLn;

  { Demonstrate IMovable interface }
  WriteLn('--- Moving shapes ---');
  MoveShape(rect, 5, 10);
  MoveShape(circle, -10, 5);
  rect.Draw;
  circle.Draw;
  WriteLn;

  { Demonstrate RTTI }
  WriteLn('--- Shape descriptions with RTTI ---');
  for i := 0 to 1 do
  begin
    DescribeShape(shapes[i])
  end;
  WriteLn;

  { Demonstrate properties }
  WriteLn('--- Property access ---');
  WriteLn('Rectangle position: (', rect.X, ',', rect.Y, ')');
  WriteLn('Circle radius: ', circle.Radius);

  { Modify via properties }
  rect.Width := 200;
  circle.Radius := 50;
  WriteLn('Modified rectangle width: ', rect.Width);
  WriteLn('Modified circle radius: ', circle.Radius);

  WriteLn;
  WriteLn('=== Demo complete ===')
end.
