{
  VTris - Pascal Version (Simplified)

  A simplified Tetris clone using only parallel arrays due to Pascal frontend limitations.
  Note: Record field access and classes are not supported in current Pascal frontend.
}
program VTris;

uses Crt;

const
  BOARD_WIDTH = 10;
  BOARD_HEIGHT = 20;
  GAME_AREA_LEFT = 5;
  GAME_AREA_TOP = 2;

  { Color codes }
  COLOR_BLACK = 0;
  COLOR_RED = 1;
  COLOR_GREEN = 2;
  COLOR_YELLOW = 3;
  COLOR_BLUE = 4;
  COLOR_MAGENTA = 5;
  COLOR_CYAN = 6;
  COLOR_WHITE = 7;

  { Piece types }
  PIECE_I = 0;
  PIECE_O = 1;
  PIECE_T = 2;
  PIECE_S = 3;
  PIECE_Z = 4;
  PIECE_J = 5;
  PIECE_L = 6;

var
  { Board - each cell stores color (0 = empty) }
  Board: array[200] of Integer;  { 10 x 20 = 200 cells }

  { Current piece data }
  PieceType: Integer;
  PieceX: Integer;
  PieceY: Integer;
  PieceRotation: Integer;

  { Game state }
  Score: Integer;
  Lines: Integer;
  Level: Integer;
  GameRunning: Integer;
  DropTimer: Integer;
  DropSpeed: Integer;
  NextPiece: Integer;

{ Get board cell at (x, y) }
function GetCell(x, y: Integer): Integer;
begin
  if (x >= 0) and (x < BOARD_WIDTH) and (y >= 0) and (y < BOARD_HEIGHT) then
    Result := Board[y * BOARD_WIDTH + x]
  else
    Result := 1;  { Out of bounds = filled }
end;

{ Set board cell at (x, y) }
procedure SetCell(x, y, value: Integer);
begin
  if (x >= 0) and (x < BOARD_WIDTH) and (y >= 0) and (y < BOARD_HEIGHT) then
    Board[y * BOARD_WIDTH + x] := value;
end;

{ Get piece block position - returns offset from piece origin }
{ Each piece has 4 blocks, indexed 0-3 }
function GetBlockX(pieceT, rot, block: Integer): Integer;
begin
  Result := 0;

  { I piece }
  if pieceT = PIECE_I then
  begin
    if (rot = 0) or (rot = 2) then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 2;
      if block = 3 then Result := 3;
    end
    else
    begin
      Result := 1;
    end;
  end;

  { O piece - no rotation }
  if pieceT = PIECE_O then
  begin
    if (block = 0) or (block = 2) then Result := 0
    else Result := 1;
  end;

  { T piece }
  if pieceT = PIECE_T then
  begin
    if rot = 0 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 2;
      if block = 3 then Result := 1;
    end;
    if rot = 1 then
    begin
      if block = 0 then Result := 1;
      if block = 1 then Result := 0;
      if block = 2 then Result := 1;
      if block = 3 then Result := 1;
    end;
    if rot = 2 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 2;
      if block = 3 then Result := 1;
    end;
    if rot = 3 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 0;
      if block = 2 then Result := 1;
      if block = 3 then Result := 0;
    end;
  end;

  { S piece }
  if pieceT = PIECE_S then
  begin
    if (rot = 0) or (rot = 2) then
    begin
      if block = 0 then Result := 1;
      if block = 1 then Result := 2;
      if block = 2 then Result := 0;
      if block = 3 then Result := 1;
    end
    else
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 0;
      if block = 2 then Result := 1;
      if block = 3 then Result := 1;
    end;
  end;

  { Z piece }
  if pieceT = PIECE_Z then
  begin
    if (rot = 0) or (rot = 2) then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end
    else
    begin
      if block = 0 then Result := 1;
      if block = 1 then Result := 0;
      if block = 2 then Result := 1;
      if block = 3 then Result := 0;
    end;
  end;

  { J piece }
  if pieceT = PIECE_J then
  begin
    if rot = 0 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 0;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end;
    if rot = 1 then
    begin
      if block = 0 then Result := 1;
      if block = 1 then Result := 0;
      if block = 2 then Result := 0;
      if block = 3 then Result := 0;
    end;
    if rot = 2 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 2;
      if block = 3 then Result := 2;
    end;
    if rot = 3 then
    begin
      if block = 0 then Result := 1;
      if block = 1 then Result := 1;
      if block = 2 then Result := 1;
      if block = 3 then Result := 0;
    end;
  end;

  { L piece }
  if pieceT = PIECE_L then
  begin
    if rot = 0 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 2;
      if block = 3 then Result := 2;
    end;
    if rot = 1 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 0;
      if block = 2 then Result := 0;
      if block = 3 then Result := 1;
    end;
    if rot = 2 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 0;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end;
    if rot = 3 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 1;
      if block = 3 then Result := 1;
    end;
  end;
end;

function GetBlockY(pieceT, rot, block: Integer): Integer;
begin
  Result := 0;

  { I piece }
  if pieceT = PIECE_I then
  begin
    if (rot = 0) or (rot = 2) then
      Result := 0
    else
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 2;
      if block = 3 then Result := 3;
    end;
  end;

  { O piece }
  if pieceT = PIECE_O then
  begin
    if (block = 0) or (block = 1) then Result := 0
    else Result := 1;
  end;

  { T piece }
  if pieceT = PIECE_T then
  begin
    if rot = 0 then
    begin
      if block = 3 then Result := 1 else Result := 0;
    end;
    if rot = 1 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end;
    if rot = 2 then
    begin
      if block = 3 then Result := 1 else Result := 0;
    end;
    if rot = 3 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end;
  end;

  { S piece }
  if pieceT = PIECE_S then
  begin
    if (rot = 0) or (rot = 2) then
    begin
      if (block = 0) or (block = 1) then Result := 0
      else Result := 1;
    end
    else
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end;
  end;

  { Z piece }
  if pieceT = PIECE_Z then
  begin
    if (rot = 0) or (rot = 2) then
    begin
      if (block = 0) or (block = 1) then Result := 0
      else Result := 1;
    end
    else
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end;
  end;

  { J piece }
  if pieceT = PIECE_J then
  begin
    if rot = 0 then
    begin
      if block = 0 then Result := 0 else Result := 1;
    end;
    if rot = 1 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end;
    if rot = 2 then
    begin
      if block = 3 then Result := 1 else Result := 0;
    end;
    if rot = 3 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 0;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end;
  end;

  { L piece }
  if pieceT = PIECE_L then
  begin
    if rot = 0 then
    begin
      if block = 3 then Result := 0 else Result := 1;
    end;
    if rot = 1 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 1;
      if block = 2 then Result := 2;
      if block = 3 then Result := 2;
    end;
    if rot = 2 then
    begin
      if block = 0 then Result := 0 else Result := 1;
    end;
    if rot = 3 then
    begin
      if block = 0 then Result := 0;
      if block = 1 then Result := 0;
      if block = 2 then Result := 1;
      if block = 3 then Result := 2;
    end;
  end;
end;

{ Get piece color }
function GetPieceColor(pieceT: Integer): Integer;
begin
  if pieceT = PIECE_I then Result := COLOR_CYAN;
  if pieceT = PIECE_O then Result := COLOR_YELLOW;
  if pieceT = PIECE_T then Result := COLOR_MAGENTA;
  if pieceT = PIECE_S then Result := COLOR_GREEN;
  if pieceT = PIECE_Z then Result := COLOR_RED;
  if pieceT = PIECE_J then Result := COLOR_BLUE;
  if pieceT = PIECE_L then Result := COLOR_WHITE;
end;

{ Check if piece fits at position }
function PieceFits(px, py, rot: Integer): Integer;
var
  i: Integer;
  bx, by: Integer;
begin
  Result := 1;
  for i := 0 to 3 do
  begin
    bx := px + GetBlockX(PieceType, rot, i);
    by := py + GetBlockY(PieceType, rot, i);
    if GetCell(bx, by) <> 0 then
      Result := 0;
  end;
end;

{ Lock piece into board }
procedure LockPiece;
var
  i: Integer;
  bx, by: Integer;
  color: Integer;
begin
  color := GetPieceColor(PieceType);
  for i := 0 to 3 do
  begin
    bx := PieceX + GetBlockX(PieceType, PieceRotation, i);
    by := PieceY + GetBlockY(PieceType, PieceRotation, i);
    SetCell(bx, by, color);
  end;
end;

{ Clear full lines }
function ClearLines: Integer;
var
  y, x: Integer;
  full: Integer;
  cleared: Integer;
  srcY: Integer;
begin
  cleared := 0;
  y := BOARD_HEIGHT - 1;
  while y >= 0 do
  begin
    full := 1;
    for x := 0 to BOARD_WIDTH - 1 do
    begin
      if GetCell(x, y) = 0 then
        full := 0;
    end;

    if full = 1 then
    begin
      cleared := cleared + 1;
      { Move all lines above down }
      srcY := y - 1;
      while srcY >= 0 do
      begin
        for x := 0 to BOARD_WIDTH - 1 do
          SetCell(x, srcY + 1, GetCell(x, srcY));
        srcY := srcY - 1;
      end;
      { Clear top line }
      for x := 0 to BOARD_WIDTH - 1 do
        SetCell(x, 0, 0);
    end
    else
      y := y - 1;
  end;
  Result := cleared;
end;

{ Spawn new piece }
procedure SpawnPiece;
begin
  PieceType := NextPiece;
  NextPiece := Trunc(Random(7));
  PieceX := BOARD_WIDTH div 2 - 1;
  PieceY := 0;
  PieceRotation := 0;

  if PieceFits(PieceX, PieceY, PieceRotation) = 0 then
    GameRunning := 0;
end;

{ Clear board }
procedure ClearBoard;
var
  i: Integer;
begin
  for i := 0 to 199 do
    Board[i] := 0;
end;

{ Draw the game }
procedure DrawGame;
var
  x, y, i: Integer;
  color: Integer;
  bx, by: Integer;
begin
  { Draw border }
  TextColor(COLOR_WHITE);
  for y := 0 to BOARD_HEIGHT + 1 do
  begin
    GotoXY(GAME_AREA_LEFT - 1, GAME_AREA_TOP + y);
    Write('#');
    GotoXY(GAME_AREA_LEFT + BOARD_WIDTH * 2, GAME_AREA_TOP + y);
    Write('#');
  end;
  for x := 0 to BOARD_WIDTH * 2 do
  begin
    GotoXY(GAME_AREA_LEFT + x, GAME_AREA_TOP - 1);
    Write('#');
    GotoXY(GAME_AREA_LEFT + x, GAME_AREA_TOP + BOARD_HEIGHT);
    Write('#');
  end;

  { Draw board }
  for y := 0 to BOARD_HEIGHT - 1 do
  begin
    for x := 0 to BOARD_WIDTH - 1 do
    begin
      GotoXY(GAME_AREA_LEFT + x * 2, GAME_AREA_TOP + y);
      color := GetCell(x, y);
      if color > 0 then
      begin
        TextColor(color);
        Write('[]');
      end
      else
      begin
        TextColor(COLOR_BLACK);
        Write('  ');
      end;
    end;
  end;

  { Draw current piece }
  color := GetPieceColor(PieceType);
  TextColor(color);
  for i := 0 to 3 do
  begin
    bx := PieceX + GetBlockX(PieceType, PieceRotation, i);
    by := PieceY + GetBlockY(PieceType, PieceRotation, i);
    if (by >= 0) and (by < BOARD_HEIGHT) then
    begin
      GotoXY(GAME_AREA_LEFT + bx * 2, GAME_AREA_TOP + by);
      Write('[]');
    end;
  end;

  { Draw info }
  TextColor(COLOR_WHITE);
  GotoXY(GAME_AREA_LEFT + BOARD_WIDTH * 2 + 4, GAME_AREA_TOP);
  Write('Score: ', Score);
  GotoXY(GAME_AREA_LEFT + BOARD_WIDTH * 2 + 4, GAME_AREA_TOP + 2);
  Write('Lines: ', Lines);
  GotoXY(GAME_AREA_LEFT + BOARD_WIDTH * 2 + 4, GAME_AREA_TOP + 4);
  Write('Level: ', Level);

  { Draw controls }
  GotoXY(GAME_AREA_LEFT + BOARD_WIDTH * 2 + 4, GAME_AREA_TOP + 8);
  Write('A/D = Move');
  GotoXY(GAME_AREA_LEFT + BOARD_WIDTH * 2 + 4, GAME_AREA_TOP + 9);
  Write('W = Rotate');
  GotoXY(GAME_AREA_LEFT + BOARD_WIDTH * 2 + 4, GAME_AREA_TOP + 10);
  Write('S = Drop');
  GotoXY(GAME_AREA_LEFT + BOARD_WIDTH * 2 + 4, GAME_AREA_TOP + 11);
  Write('Q = Quit');
end;

{ Handle input }
procedure HandleInput;
var
  Key: String;
  newRot: Integer;
begin
  Key := InKey;

  if Length(Key) > 0 then
  begin
    { Move left }
    if (Key = 'a') or (Key = 'A') then
    begin
      if PieceFits(PieceX - 1, PieceY, PieceRotation) = 1 then
        PieceX := PieceX - 1;
    end;

    { Move right }
    if (Key = 'd') or (Key = 'D') then
    begin
      if PieceFits(PieceX + 1, PieceY, PieceRotation) = 1 then
        PieceX := PieceX + 1;
    end;

    { Rotate }
    if (Key = 'w') or (Key = 'W') then
    begin
      newRot := (PieceRotation + 1) mod 4;
      if PieceFits(PieceX, PieceY, newRot) = 1 then
        PieceRotation := newRot;
    end;

    { Soft drop }
    if (Key = 's') or (Key = 'S') then
    begin
      if PieceFits(PieceX, PieceY + 1, PieceRotation) = 1 then
        PieceY := PieceY + 1;
    end;

    { Hard drop }
    if Key = ' ' then
    begin
      while PieceFits(PieceX, PieceY + 1, PieceRotation) = 1 do
        PieceY := PieceY + 1;
    end;

    { Quit }
    if (Key = 'q') or (Key = 'Q') then
      GameRunning := 0;
  end;
end;

{ Update game logic }
procedure UpdateGame;
var
  cleared: Integer;
begin
  DropTimer := DropTimer + 1;

  if DropTimer >= DropSpeed then
  begin
    DropTimer := 0;

    if PieceFits(PieceX, PieceY + 1, PieceRotation) = 1 then
    begin
      PieceY := PieceY + 1;
    end
    else
    begin
      { Lock piece }
      LockPiece;

      { Clear lines }
      cleared := ClearLines;
      if cleared > 0 then
      begin
        Lines := Lines + cleared;
        Score := Score + cleared * cleared * 100;
        Level := Lines div 10 + 1;
        DropSpeed := 20 - Level * 2;
        if DropSpeed < 2 then
          DropSpeed := 2;
      end;

      { Spawn new piece }
      SpawnPiece;
    end;
  end;
end;

{ Initialize game }
procedure InitGame;
begin
  Score := 0;
  Lines := 0;
  Level := 1;
  GameRunning := 1;
  DropTimer := 0;
  DropSpeed := 20;

  ClearBoard;
  Randomize;
  NextPiece := Trunc(Random(7));
  SpawnPiece;
end;

{ Game loop }
procedure GameLoop;
begin
  ClrScr;
  HideCursor;

  while GameRunning = 1 do
  begin
    DrawGame;
    HandleInput;
    UpdateGame;
    Delay(50);
  end;

  { Game over }
  ClrScr;
  ShowCursor;
  WriteLn;
  WriteLn('=== GAME OVER ===');
  WriteLn;
  WriteLn('Final Score: ', Score);
  WriteLn('Lines: ', Lines);
  WriteLn('Level: ', Level);
  WriteLn;
  WriteLn('Press any key to exit...');
  ReadKey;
end;

{ Main program }
begin
  WriteLn('=== VTRIS ===');
  WriteLn('Pascal Edition');
  WriteLn;
  WriteLn('Controls:');
  WriteLn('  A/D - Move left/right');
  WriteLn('  W   - Rotate');
  WriteLn('  S   - Soft drop');
  WriteLn('  Space - Hard drop');
  WriteLn('  Q   - Quit');
  WriteLn;
  WriteLn('Press any key to start...');
  ReadKey;

  InitGame;
  GameLoop;
end.
