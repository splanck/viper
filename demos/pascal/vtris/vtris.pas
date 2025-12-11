{====================================================================
 vTRIS - Tetris Clone for Viper Pascal
 ====================================================================
 A complete implementation using proper arrays (Bug #20 now fixed!)
 ====================================================================}

program VTris;

uses Crt;

const
  BOARD_WIDTH = 10;
  BOARD_HEIGHT = 20;
  NUM_PIECE_TYPES = 7;
  PIECE_SIZE = 4;

{ Colors }
  COLOR_BLACK = 0;
  COLOR_RED = 1;
  COLOR_GREEN = 2;
  COLOR_YELLOW = 3;
  COLOR_BLUE = 4;
  COLOR_MAGENTA = 5;
  COLOR_CYAN = 6;
  COLOR_WHITE = 7;

{====================================================================
 Global Variables - Now using proper 2D arrays!
 ====================================================================}
var
  { Board grid - stored as 1D array indexed [row * BOARD_WIDTH + col] }
  Board: array[200] of Integer;      { 20 rows x 10 cols = 200 cells }
  BoardColor: array[200] of Integer; { Color for each cell }

  { Current piece state }
  Piece: array[16] of Integer;       { 4x4 grid = 16 cells }
  PieceType, PieceColor, PieceRotation: Integer;
  PieceX, PieceY: Integer;

  { Next piece state }
  NextPiece: array[16] of Integer;
  NextPieceType, NextPieceColor: Integer;

  { Game state }
  Score, Lines, Level: Integer;
  GameOver, DropCounter, DropSpeed: Integer;
  Running: Integer;

{====================================================================
 Grid Access Helpers
 ====================================================================}
function GetBoard(row, col: Integer): Integer;
var
  idx: Integer;
begin
  if (row < 0) or (row >= BOARD_HEIGHT) or (col < 0) or (col >= BOARD_WIDTH) then
    Result := 1  { Out of bounds = filled }
  else
  begin
    idx := row * BOARD_WIDTH + col;
    Result := Board[idx];
  end;
end;

procedure SetBoard(row, col, val: Integer);
var
  idx: Integer;
begin
  if (row >= 0) and (row < BOARD_HEIGHT) and (col >= 0) and (col < BOARD_WIDTH) then
  begin
    idx := row * BOARD_WIDTH + col;
    Board[idx] := val;
  end;
end;

function GetBoardColor(row, col: Integer): Integer;
var
  idx: Integer;
begin
  if (row < 0) or (row >= BOARD_HEIGHT) or (col < 0) or (col >= BOARD_WIDTH) then
    Result := 0
  else
  begin
    idx := row * BOARD_WIDTH + col;
    Result := BoardColor[idx];
  end;
end;

procedure SetBoardColor(row, col, val: Integer);
var
  idx: Integer;
begin
  if (row >= 0) and (row < BOARD_HEIGHT) and (col >= 0) and (col < BOARD_WIDTH) then
  begin
    idx := row * BOARD_WIDTH + col;
    BoardColor[idx] := val;
  end;
end;

function GetPieceCell(row, col: Integer): Integer;
var
  idx: Integer;
begin
  idx := row * 4 + col;
  Result := Piece[idx];
end;

procedure SetPieceCell(row, col, val: Integer);
var
  idx: Integer;
begin
  idx := row * 4 + col;
  Piece[idx] := val;
end;

{====================================================================
 Initialize a piece by type (0-6)
 Piece shapes: I, O, T, S, Z, J, L
 ====================================================================}
procedure InitPieceShape(ptype: Integer);
var
  I: Integer;
begin
  { Clear piece }
  for I := 0 to 15 do
    Piece[I] := 0;

  case ptype of
    0: begin { I piece - cyan }
      PieceColor := COLOR_CYAN;
      Piece[1] := 1;
      Piece[5] := 1;
      Piece[9] := 1;
      Piece[13] := 1;
    end;
    1: begin { O piece - yellow }
      PieceColor := COLOR_YELLOW;
      Piece[5] := 1;
      Piece[6] := 1;
      Piece[9] := 1;
      Piece[10] := 1;
    end;
    2: begin { T piece - magenta }
      PieceColor := COLOR_MAGENTA;
      Piece[5] := 1;
      Piece[8] := 1;
      Piece[9] := 1;
      Piece[10] := 1;
    end;
    3: begin { S piece - green }
      PieceColor := COLOR_GREEN;
      Piece[5] := 1;
      Piece[6] := 1;
      Piece[8] := 1;
      Piece[9] := 1;
    end;
    4: begin { Z piece - red }
      PieceColor := COLOR_RED;
      Piece[4] := 1;
      Piece[5] := 1;
      Piece[9] := 1;
      Piece[10] := 1;
    end;
    5: begin { J piece - blue }
      PieceColor := COLOR_BLUE;
      Piece[4] := 1;
      Piece[8] := 1;
      Piece[9] := 1;
      Piece[10] := 1;
    end;
    6: begin { L piece - white }
      PieceColor := COLOR_WHITE;
      Piece[6] := 1;
      Piece[8] := 1;
      Piece[9] := 1;
      Piece[10] := 1;
    end;
  end;
  PieceType := ptype;
  PieceRotation := 0;
end;

procedure InitNextPieceShape(ptype: Integer);
var
  I: Integer;
begin
  { Clear next piece }
  for I := 0 to 15 do
    NextPiece[I] := 0;

  case ptype of
    0: begin { I piece }
      NextPieceColor := COLOR_CYAN;
      NextPiece[1] := 1;
      NextPiece[5] := 1;
      NextPiece[9] := 1;
      NextPiece[13] := 1;
    end;
    1: begin { O piece }
      NextPieceColor := COLOR_YELLOW;
      NextPiece[5] := 1;
      NextPiece[6] := 1;
      NextPiece[9] := 1;
      NextPiece[10] := 1;
    end;
    2: begin { T piece }
      NextPieceColor := COLOR_MAGENTA;
      NextPiece[5] := 1;
      NextPiece[8] := 1;
      NextPiece[9] := 1;
      NextPiece[10] := 1;
    end;
    3: begin { S piece }
      NextPieceColor := COLOR_GREEN;
      NextPiece[5] := 1;
      NextPiece[6] := 1;
      NextPiece[8] := 1;
      NextPiece[9] := 1;
    end;
    4: begin { Z piece }
      NextPieceColor := COLOR_RED;
      NextPiece[4] := 1;
      NextPiece[5] := 1;
      NextPiece[9] := 1;
      NextPiece[10] := 1;
    end;
    5: begin { J piece }
      NextPieceColor := COLOR_BLUE;
      NextPiece[4] := 1;
      NextPiece[8] := 1;
      NextPiece[9] := 1;
      NextPiece[10] := 1;
    end;
    6: begin { L piece }
      NextPieceColor := COLOR_WHITE;
      NextPiece[6] := 1;
      NextPiece[8] := 1;
      NextPiece[9] := 1;
      NextPiece[10] := 1;
    end;
  end;
  NextPieceType := ptype;
end;

{====================================================================
 Rotate piece clockwise
 ====================================================================}
procedure RotatePiece;
var
  Temp: array[16] of Integer;
  row, col, srcIdx, dstIdx: Integer;
begin
  { Copy current piece to temp with 90 degree rotation }
  for row := 0 to 3 do
  begin
    for col := 0 to 3 do
    begin
      srcIdx := row * 4 + col;
      dstIdx := col * 4 + (3 - row);
      Temp[dstIdx] := Piece[srcIdx];
    end;
  end;

  { Copy back }
  for row := 0 to 15 do
    Piece[row] := Temp[row];

  PieceRotation := (PieceRotation + 1) mod 4;
end;

{====================================================================
 Check if piece can be placed at position
 ====================================================================}
function CanPlace(checkX, checkY: Integer): Integer;
var
  row, col, idx: Integer;
  boardRow, boardCol: Integer;
begin
  Result := 1;
  for row := 0 to 3 do
  begin
    for col := 0 to 3 do
    begin
      idx := row * 4 + col;
      if Piece[idx] = 1 then
      begin
        boardRow := checkY + row;
        boardCol := checkX + col;
        if (boardCol < 0) or (boardCol >= BOARD_WIDTH) then
          Result := 0
        else if boardRow >= BOARD_HEIGHT then
          Result := 0
        else if (boardRow >= 0) and (GetBoard(boardRow, boardCol) = 1) then
          Result := 0;
      end;
    end;
  end;
end;

{====================================================================
 Lock piece to board
 ====================================================================}
procedure LockPiece;
var
  row, col, idx: Integer;
  boardRow, boardCol: Integer;
begin
  for row := 0 to 3 do
  begin
    for col := 0 to 3 do
    begin
      idx := row * 4 + col;
      if Piece[idx] = 1 then
      begin
        boardRow := PieceY + row;
        boardCol := PieceX + col;
        SetBoard(boardRow, boardCol, 1);
        SetBoardColor(boardRow, boardCol, PieceColor);
      end;
    end;
  end;
end;

{====================================================================
 Clear completed lines and return count
 ====================================================================}
function ClearLines: Integer;
var
  r, c, dr: Integer;
  full: Integer;
  cleared: Integer;
begin
  cleared := 0;
  r := BOARD_HEIGHT - 1;

  while r >= 0 do
  begin
    { Check if row is full }
    full := 1;
    c := 0;
    while c < BOARD_WIDTH do
    begin
      if GetBoard(r, c) = 0 then
        full := 0;
      c := c + 1;
    end;

    if full = 1 then
    begin
      cleared := cleared + 1;
      { Move all rows above down by one }
      dr := r;
      while dr >= 1 do
      begin
        c := 0;
        while c < BOARD_WIDTH do
        begin
          SetBoard(dr, c, GetBoard(dr - 1, c));
          SetBoardColor(dr, c, GetBoardColor(dr - 1, c));
          c := c + 1;
        end;
        dr := dr - 1;
      end;
      { Clear top row }
      c := 0;
      while c < BOARD_WIDTH do
      begin
        SetBoard(0, c, 0);
        SetBoardColor(0, c, 0);
        c := c + 1;
      end;
      { Don't decrement r - check same row again }
    end
    else
      r := r - 1;
  end;

  Result := cleared;
end;

{====================================================================
 Spawn new piece
 ====================================================================}
procedure SpawnPiece;
var
  I: Integer;
begin
  { Copy next piece to current }
  PieceType := NextPieceType;
  PieceColor := NextPieceColor;
  for I := 0 to 15 do
    Piece[I] := NextPiece[I];
  PieceRotation := 0;

  { Generate new next piece }
  InitNextPieceShape(Trunc(Random * NUM_PIECE_TYPES));

  { Start position }
  PieceX := (BOARD_WIDTH - 4) div 2;
  PieceY := 0;

  { Check game over }
  if CanPlace(PieceX, PieceY) = 0 then
    GameOver := 1;
end;

{====================================================================
 Initialize game state
 ====================================================================}
procedure InitGame;
var
  I: Integer;
begin
  { Clear board }
  for I := 0 to 199 do
  begin
    Board[I] := 0;
    BoardColor[I] := 0;
  end;

  { Reset game state }
  Score := 0;
  Lines := 0;
  Level := 1;
  GameOver := 0;
  DropCounter := 0;
  DropSpeed := 20;

  { Generate first piece }
  InitPieceShape(Trunc(Random * NUM_PIECE_TYPES));
  PieceX := (BOARD_WIDTH - 4) div 2;
  PieceY := 0;

  { Generate next piece }
  InitNextPieceShape(Trunc(Random * NUM_PIECE_TYPES));
end;

{====================================================================
 Draw the game board
 ====================================================================}
procedure DrawBoard;
var
  row, col: Integer;
  cellVal, cellColor: Integer;
begin

  { Draw board border }
  GotoXY(4, 2);
  TextColor(COLOR_WHITE);
  Write('+');
  for col := 1 to BOARD_WIDTH * 2 do
    Write('-');
  Write('+');

  for row := 0 to BOARD_HEIGHT - 1 do
  begin
    GotoXY(4, 3 + row);
    TextColor(COLOR_WHITE);
    Write('|');
    for col := 0 to BOARD_WIDTH - 1 do
    begin
      cellVal := GetBoard(row, col);
      if cellVal = 1 then
      begin
        cellColor := GetBoardColor(row, col);
        TextColor(cellColor);
        Write('[]');
      end
      else
      begin
        TextColor(0);
        Write('  ');
      end;
    end;
    TextColor(COLOR_WHITE);
    Write('|');
  end;

  GotoXY(4, 23);
  Write('+');
  for col := 1 to BOARD_WIDTH * 2 do
    Write('-');
  Write('+');

end;

{====================================================================
 Draw the current falling piece
 ====================================================================}
procedure DrawPiece;
var
  row, col, idx: Integer;
  screenX, screenY: Integer;
begin
  TextColor(PieceColor);

  for row := 0 to 3 do
  begin
    for col := 0 to 3 do
    begin
      idx := row * 4 + col;
      if Piece[idx] = 1 then
      begin
        screenY := 3 + PieceY + row;
        screenX := 5 + (PieceX + col) * 2;
        if (screenY >= 3) and (screenY <= 22) then
        begin
          GotoXY(screenX, screenY);
          Write('[]');
        end;
      end;
    end;
  end;

  TextColor(COLOR_WHITE);
end;

{====================================================================
 Draw UI (score, next piece, etc)
 ====================================================================}
procedure DrawUI;
var
  row, col, idx: Integer;
begin

  { Score box }
  GotoXY(30, 3);
  TextColor(COLOR_CYAN);
  Write('+----------+');
  GotoXY(30, 4);
  Write('|  SCORE   |');
  GotoXY(30, 5);
  Write('+----------+');
  GotoXY(30, 6);
  TextColor(COLOR_WHITE);
  Write('| ');
  Write(Score);
  GotoXY(41, 6);
  TextColor(COLOR_CYAN);
  Write('|');

  GotoXY(30, 7);
  Write('+----------+');

  { Lines box }
  GotoXY(30, 9);
  TextColor(COLOR_GREEN);
  Write('+----------+');
  GotoXY(30, 10);
  Write('|  LINES   |');
  GotoXY(30, 11);
  Write('+----------+');
  GotoXY(30, 12);
  TextColor(COLOR_WHITE);
  Write('| ');
  Write(Lines);
  GotoXY(41, 12);
  TextColor(COLOR_GREEN);
  Write('|');
  GotoXY(30, 13);
  Write('+----------+');

  { Level box }
  GotoXY(30, 15);
  TextColor(COLOR_YELLOW);
  Write('+----------+');
  GotoXY(30, 16);
  Write('|  LEVEL   |');
  GotoXY(30, 17);
  Write('+----------+');
  GotoXY(30, 18);
  TextColor(COLOR_WHITE);
  Write('| ');
  Write(Level);
  GotoXY(41, 18);
  TextColor(COLOR_YELLOW);
  Write('|');
  GotoXY(30, 19);
  Write('+----------+');

  { Next piece preview }
  GotoXY(46, 3);
  TextColor(COLOR_MAGENTA);
  Write('+--------+');
  GotoXY(46, 4);
  Write('|  NEXT  |');
  GotoXY(46, 5);
  Write('+--------+');

  for row := 0 to 3 do
  begin
    GotoXY(46, 6 + row);
    TextColor(COLOR_MAGENTA);
    Write('|');
    for col := 0 to 3 do
    begin
      idx := row * 4 + col;
      if NextPiece[idx] = 1 then
      begin
        TextColor(NextPieceColor);
        Write('[]');
      end
      else
        Write('  ');
    end;
    TextColor(COLOR_MAGENTA);
    Write('|');
  end;

  GotoXY(46, 10);
  Write('+--------+');

end;

{====================================================================
 Game loop
 ====================================================================}
procedure GameLoop;
var
  Key: String;
  clearedLines: Integer;
begin
  while GameOver = 0 do
  begin
    ClrScr;
    DrawBoard;
    DrawPiece;
    DrawUI;

    { Handle input }
    Key := InKey;
    if Length(Key) > 0 then
    begin
      if (Key = 'a') or (Key = 'A') then
      begin
        if CanPlace(PieceX - 1, PieceY) = 1 then
          PieceX := PieceX - 1;
      end
      else if (Key = 'd') or (Key = 'D') then
      begin
        if CanPlace(PieceX + 1, PieceY) = 1 then
          PieceX := PieceX + 1;
      end
      else if (Key = 's') or (Key = 'S') then
      begin
        if CanPlace(PieceX, PieceY + 1) = 1 then
        begin
          PieceY := PieceY + 1;
          Score := Score + 1;
        end;
      end
      else if (Key = 'w') or (Key = 'W') then
      begin
        RotatePiece;
        if CanPlace(PieceX, PieceY) = 0 then
        begin
          { Rotate back if can't place }
          RotatePiece;
          RotatePiece;
          RotatePiece;
        end;
      end
      else if Key = ' ' then
      begin
        { Hard drop }
        while CanPlace(PieceX, PieceY + 1) = 1 do
        begin
          PieceY := PieceY + 1;
          Score := Score + 2;
        end;
      end
      else if (Key = 'q') or (Key = 'Q') then
        GameOver := 1;
    end;

    { Auto drop }
    DropCounter := DropCounter + 1;
    if DropCounter >= DropSpeed then
    begin
      DropCounter := 0;
      if CanPlace(PieceX, PieceY + 1) = 1 then
        PieceY := PieceY + 1
      else
      begin
        { Lock piece and check for lines }
        LockPiece;
        clearedLines := ClearLines;
        if clearedLines > 0 then
        begin
          Lines := Lines + clearedLines;
          { Score based on lines cleared }
          case clearedLines of
            1: Score := Score + 100 * Level;
            2: Score := Score + 300 * Level;
            3: Score := Score + 500 * Level;
            4: Score := Score + 800 * Level;
          end;
          { Level up every 10 lines }
          Level := (Lines div 10) + 1;
          if Level > 20 then Level := 20;
          DropSpeed := 21 - Level;
          if DropSpeed < 1 then DropSpeed := 1;
        end;
        SpawnPiece;
      end;
    end;

    Delay(50);
  end;
end;

{====================================================================
 Main Menu
 ====================================================================}
procedure ShowMainMenu;
begin
  ClrScr;
  TextColor(COLOR_CYAN);
  GotoXY(15, 5);
  WriteLn('=============================');
  GotoXY(15, 6);
  WriteLn('         v T R I S          ');
  GotoXY(15, 7);
  WriteLn('     (Viper Pascal Demo)    ');
  GotoXY(15, 8);
  WriteLn('=============================');

  TextColor(COLOR_WHITE);
  GotoXY(18, 11);
  WriteLn('1. START GAME');
  GotoXY(18, 13);
  WriteLn('Q. QUIT');

  TextColor(COLOR_YELLOW);
  GotoXY(15, 16);
  WriteLn('Controls:');
  TextColor(COLOR_WHITE);
  GotoXY(15, 17);
  WriteLn('A/D - Move left/right');
  GotoXY(15, 18);
  WriteLn('W   - Rotate');
  GotoXY(15, 19);
  WriteLn('S   - Soft drop');
  GotoXY(15, 20);
  WriteLn('Space - Hard drop');

  GotoXY(1, 23);
end;

{====================================================================
 Game Over Screen
 ====================================================================}
procedure GameOverScreen;
var
  k: String;
begin
  TextColor(COLOR_WHITE);
  ClrScr;

  GotoXY(10, 8);
  TextColor(COLOR_RED);
  WriteLn('+=====================+');
  GotoXY(10, 9);
  WriteLn('|     GAME OVER!      |');
  GotoXY(10, 10);
  WriteLn('+=====================+');

  GotoXY(12, 12);
  TextColor(COLOR_CYAN);
  Write('Final Score: ');
  TextColor(COLOR_WHITE);
  WriteLn(Score);

  GotoXY(12, 13);
  TextColor(COLOR_GREEN);
  Write('Lines: ');
  TextColor(COLOR_WHITE);
  WriteLn(Lines);

  GotoXY(12, 14);
  TextColor(COLOR_YELLOW);
  Write('Level: ');
  TextColor(COLOR_WHITE);
  WriteLn(Level);

  GotoXY(10, 17);
  TextColor(COLOR_WHITE);
  Write('Press any key...');

  k := '';
  while k = '' do
    k := InKey;
end;

{====================================================================
 Main Program
 ====================================================================}
var
  menuChoice: String;

begin
  Randomize;
  HideCursor;

  Running := 1;

  while Running = 1 do
  begin
    ShowMainMenu;

    menuChoice := '';
    while menuChoice = '' do
      menuChoice := InKey;

    if menuChoice = '1' then
    begin
      InitGame;
      GameLoop;
      GameOverScreen;
    end
    else if (menuChoice = 'q') or (menuChoice = 'Q') then
      Running := 0;
  end;

  ClrScr;
  ShowCursor;
  GotoXY(10, 12);
  TextColor(COLOR_CYAN);
  WriteLn('Thanks for playing vTRIS!');
  GotoXY(10, 13);
  TextColor(COLOR_WHITE);
  WriteLn('A Viper Pascal Demo');
  GotoXY(1, 15);
end.
