{====================================================================
 SNAKE - Classic Snake Game for Viper Pascal
 ====================================================================
 A complete implementation using proper arrays
 ====================================================================}

program Snake;

uses Crt;

const
  FIELD_WIDTH = 50;
  FIELD_HEIGHT = 20;
  MAX_SNAKE_LEN = 200;

{ Colors }
  COLOR_BLACK = 0;
  COLOR_RED = 1;
  COLOR_GREEN = 2;
  COLOR_YELLOW = 3;
  COLOR_BLUE = 4;
  COLOR_MAGENTA = 5;
  COLOR_CYAN = 6;
  COLOR_WHITE = 7;

{ Directions }
  DIR_UP = 0;
  DIR_DOWN = 1;
  DIR_LEFT = 2;
  DIR_RIGHT = 3;

{====================================================================
 Global Variables
 ====================================================================}
var
  { Snake body segments [x, y] }
  SnakeX: array[MAX_SNAKE_LEN] of Integer;
  SnakeY: array[MAX_SNAKE_LEN] of Integer;
  SnakeLen: Integer;
  SnakeDir: Integer;
  NextDir: Integer;

  { Food position }
  FoodX, FoodY: Integer;

  { Game state }
  Score: Integer;
  Level: Integer;
  Speed: Integer;
  GameOver: Integer;
  Running: Integer;

{====================================================================
 Spawn food at random location
 ====================================================================}
procedure SpawnFood;
var
  valid: Integer;
  i: Integer;
begin
  valid := 0;
  while valid = 0 do
  begin
    FoodX := 2 + Trunc(Random * (FIELD_WIDTH - 4));
    FoodY := 2 + Trunc(Random * (FIELD_HEIGHT - 4));

    { Check if food is not on snake }
    valid := 1;
    i := 0;
    while i < SnakeLen do
    begin
      if (SnakeX[i] = FoodX) and (SnakeY[i] = FoodY) then
        valid := 0;
      i := i + 1;
    end;
  end;
end;

{====================================================================
 Initialize game
 ====================================================================}
procedure InitGame;
var
  i: Integer;
begin
  { Initialize snake in center }
  SnakeLen := 5;
  i := 0;
  while i < SnakeLen do
  begin
    SnakeX[i] := FIELD_WIDTH div 2 - i;
    SnakeY[i] := FIELD_HEIGHT div 2;
    i := i + 1;
  end;

  SnakeDir := DIR_RIGHT;
  NextDir := DIR_RIGHT;

  Score := 0;
  Level := 1;
  Speed := 100;
  GameOver := 0;

  SpawnFood;
end;

{====================================================================
 Update snake position
 ====================================================================}
procedure UpdateSnake;
var
  i: Integer;
  newX, newY: Integer;
  ate: Integer;
begin
  { Apply next direction }
  SnakeDir := NextDir;

  { Calculate new head position }
  newX := SnakeX[0];
  newY := SnakeY[0];

  case SnakeDir of
    DIR_UP: newY := newY - 1;
    DIR_DOWN: newY := newY + 1;
    DIR_LEFT: newX := newX - 1;
    DIR_RIGHT: newX := newX + 1;
  end;

  { Check wall collision }
  if (newX < 2) or (newX >= FIELD_WIDTH - 1) or
     (newY < 2) or (newY >= FIELD_HEIGHT) then
  begin
    GameOver := 1;
    Exit;
  end;

  { Check self collision }
  i := 0;
  while i < SnakeLen do
  begin
    if (SnakeX[i] = newX) and (SnakeY[i] = newY) then
    begin
      GameOver := 1;
      Exit;
    end;
    i := i + 1;
  end;

  { Check if ate food }
  ate := 0;
  if (newX = FoodX) and (newY = FoodY) then
  begin
    ate := 1;
    Score := Score + 10;

    { Increase speed every 50 points }
    if (Score mod 50) = 0 then
    begin
      Level := Level + 1;
      if Speed > 30 then
        Speed := Speed - 10;
    end;

    { Grow snake }
    if SnakeLen < MAX_SNAKE_LEN then
      SnakeLen := SnakeLen + 1;

    SpawnFood;
  end;

  { Move body - shift all segments }
  i := SnakeLen - 1;
  while i > 0 do
  begin
    SnakeX[i] := SnakeX[i - 1];
    SnakeY[i] := SnakeY[i - 1];
    i := i - 1;
  end;

  { Set new head position }
  SnakeX[0] := newX;
  SnakeY[0] := newY;
end;

{====================================================================
 Draw the game
 ====================================================================}
procedure DrawGame;
var
  i, x: Integer;
begin
  ClrScr;

  { Draw border }
  TextColor(COLOR_WHITE);
  GotoXY(1, 1);
  Write('+');
  x := 0;
  while x < FIELD_WIDTH - 2 do
  begin
    Write('-');
    x := x + 1;
  end;
  Write('+');

  i := 2;
  while i < FIELD_HEIGHT do
  begin
    GotoXY(1, i);
    Write('|');
    GotoXY(FIELD_WIDTH, i);
    Write('|');
    i := i + 1;
  end;

  GotoXY(1, FIELD_HEIGHT);
  Write('+');
  x := 0;
  while x < FIELD_WIDTH - 2 do
  begin
    Write('-');
    x := x + 1;
  end;
  Write('+');

  { Draw food }
  TextColor(COLOR_RED);
  GotoXY(FoodX, FoodY);
  Write('*');

  { Draw snake }
  i := 0;
  while i < SnakeLen do
  begin
    if i = 0 then
    begin
      TextColor(COLOR_YELLOW);
      GotoXY(SnakeX[i], SnakeY[i]);
      Write('@');
    end
    else
    begin
      TextColor(COLOR_GREEN);
      GotoXY(SnakeX[i], SnakeY[i]);
      Write('O');
    end;
    i := i + 1;
  end;

  { Draw status }
  GotoXY(2, FIELD_HEIGHT + 1);
  TextColor(COLOR_CYAN);
  Write('Score: ');
  TextColor(COLOR_WHITE);
  Write(Score);
  Write('  ');
  TextColor(COLOR_YELLOW);
  Write('Level: ');
  TextColor(COLOR_WHITE);
  Write(Level);
  Write('  ');
  TextColor(COLOR_GREEN);
  Write('Length: ');
  TextColor(COLOR_WHITE);
  Write(SnakeLen);
  Write('  [WASD/Arrows]=Move [Q]=Quit');
end;

{====================================================================
 Game loop
 ====================================================================}
procedure GameLoop;
var
  Key: String;
begin
  while GameOver = 0 do
  begin
    DrawGame;

    { Handle input - multiple times per frame for responsiveness }
    Key := InKey;
    if Length(Key) > 0 then
    begin
      if ((Key = 'w') or (Key = 'W')) and (SnakeDir <> DIR_DOWN) then
        NextDir := DIR_UP
      else if ((Key = 's') or (Key = 'S')) and (SnakeDir <> DIR_UP) then
        NextDir := DIR_DOWN
      else if ((Key = 'a') or (Key = 'A')) and (SnakeDir <> DIR_RIGHT) then
        NextDir := DIR_LEFT
      else if ((Key = 'd') or (Key = 'D')) and (SnakeDir <> DIR_LEFT) then
        NextDir := DIR_RIGHT
      else if (Key = 'q') or (Key = 'Q') then
        GameOver := 1;
    end;

    { Update game state }
    UpdateSnake;

    Delay(Speed);
  end;
end;

{====================================================================
 Main Menu
 ====================================================================}
procedure ShowMainMenu;
begin
  ClrScr;
  TextColor(COLOR_GREEN);
  GotoXY(15, 5);
  WriteLn('+=========================+');
  GotoXY(15, 6);
  WriteLn('|                         |');
  GotoXY(15, 7);
  Write('|');
  TextColor(COLOR_YELLOW);
  Write('       S N A K E         ');
  TextColor(COLOR_GREEN);
  WriteLn('|');
  GotoXY(15, 8);
  WriteLn('|                         |');
  GotoXY(15, 9);
  WriteLn('+=========================+');

  TextColor(COLOR_GREEN);
  GotoXY(20, 11);
  WriteLn('@OOOOOOO');

  TextColor(COLOR_WHITE);
  GotoXY(18, 14);
  WriteLn('[1] NEW GAME');
  GotoXY(18, 16);
  WriteLn('[Q] QUIT');

  TextColor(COLOR_YELLOW);
  GotoXY(15, 19);
  WriteLn('Controls: WASD to move');

  TextColor(COLOR_CYAN);
  GotoXY(13, 21);
  WriteLn('   Viper Pascal Demo 2025');

  GotoXY(1, 23);
end;

{====================================================================
 Game Over Screen
 ====================================================================}
procedure GameOverScreen;
var
  k: String;
begin
  ClrScr;

  GotoXY(15, 8);
  TextColor(COLOR_RED);
  WriteLn('+=====================+');
  GotoXY(15, 9);
  WriteLn('|     GAME OVER!      |');
  GotoXY(15, 10);
  WriteLn('+=====================+');

  GotoXY(17, 12);
  TextColor(COLOR_CYAN);
  Write('Final Score: ');
  TextColor(COLOR_WHITE);
  WriteLn(Score);

  GotoXY(17, 13);
  TextColor(COLOR_GREEN);
  Write('Snake Length: ');
  TextColor(COLOR_WHITE);
  WriteLn(SnakeLen);

  GotoXY(17, 14);
  TextColor(COLOR_YELLOW);
  Write('Level Reached: ');
  TextColor(COLOR_WHITE);
  WriteLn(Level);

  GotoXY(15, 17);
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
  AltScreen(1);  { Enable alt screen with batch mode for smooth rendering }
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

  ShowCursor;
  AltScreen(0);  { Exit alt screen }
  ClrScr;
  GotoXY(15, 12);
  TextColor(COLOR_GREEN);
  WriteLn('Thanks for playing SNAKE!');
  GotoXY(15, 13);
  TextColor(COLOR_WHITE);
  WriteLn('A Viper Pascal Demo');
  GotoXY(1, 15);
end.
