{====================================================================
 CLASSIC FROGGER - Pascal Port
 ====================================================================
 A complete implementation using arrays (Bug #17/18 now fixed!)
 ====================================================================}

program Frogger;

uses Crt;

{====================================================================
 Constants
 ====================================================================}
const
  GAME_WIDTH = 70;
  GAME_HEIGHT = 24;
  NUM_VEHICLES = 10;
  NUM_PLATFORMS = 10;
  NUM_HOMES = 5;

  { Row definitions }
  HOME_ROW = 2;
  RIVER_START = 4;
  RIVER_END = 8;
  MEDIAN_ROW = 10;
  ROAD_START = 12;
  ROAD_END = 16;
  START_ROW = 18;

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
 Global Variables - Now using proper arrays!
 ====================================================================}
var
  { Frog state }
  FrogRow, FrogCol: Integer;
  FrogStartRow, FrogStartCol: Integer;
  FrogLives: Integer;
  FrogAlive: Integer;

  { Vehicle arrays (row, col, speed, dir, width, isTruck) }
  VehRow: array[NUM_VEHICLES] of Integer;
  VehCol: array[NUM_VEHICLES] of Integer;
  VehSpd: array[NUM_VEHICLES] of Integer;
  VehDir: array[NUM_VEHICLES] of Integer;
  VehW: array[NUM_VEHICLES] of Integer;

  { Platform arrays (row, col, speed, dir, width, isTurtle) }
  PlatRow: array[NUM_PLATFORMS] of Integer;
  PlatCol: array[NUM_PLATFORMS] of Integer;
  PlatSpd: array[NUM_PLATFORMS] of Integer;
  PlatDir: array[NUM_PLATFORMS] of Integer;
  PlatW: array[NUM_PLATFORMS] of Integer;
  PlatIsTurtle: array[NUM_PLATFORMS] of Integer;

  { Home arrays }
  HomeCol: array[NUM_HOMES] of Integer;
  HomeFilled: array[NUM_HOMES] of Integer;

  { Game state }
  Score: Integer;
  GameRunning: Integer;
  HomesFilledCount: Integer;

{====================================================================
 Helper Procedures
 ====================================================================}
procedure PrintAt(Row, Col: Integer; Text: String);
begin
  GotoXY(Col, Row);
  Write(Text);
end;

procedure PrintColorAt(Row, Col: Integer; Color: Integer; Text: String);
begin
  GotoXY(Col, Row);
  TextColor(Color);
  Write(Text);
  TextColor(COLOR_WHITE);
end;

{====================================================================
 Check Collision with object at position
 ====================================================================}
function CheckCollision(ObjRow, ObjCol, ObjW, FRow, FCol: Integer): Integer;
var
  J: Integer;
begin
  Result := 0;
  if FRow = ObjRow then
  begin
    for J := 0 to ObjW - 1 do
    begin
      if FCol = ObjCol + J then
      begin
        Result := 1;
        Exit;
      end;
    end;
  end;
end;

{====================================================================
 Initialize Game
 ====================================================================}
procedure InitGame;
var
  I: Integer;
begin
  Score := 0;
  GameRunning := 1;
  HomesFilledCount := 0;

  { Initialize frog }
  FrogRow := START_ROW;
  FrogCol := 35;
  FrogStartRow := START_ROW;
  FrogStartCol := 35;
  FrogLives := 3;
  FrogAlive := 1;

  { Initialize homes using arrays }
  HomeCol[0] := 8;  HomeFilled[0] := 0;
  HomeCol[1] := 20; HomeFilled[1] := 0;
  HomeCol[2] := 32; HomeFilled[2] := 0;
  HomeCol[3] := 44; HomeFilled[3] := 0;
  HomeCol[4] := 56; HomeFilled[4] := 0;

  { Initialize vehicles using arrays }
  { Lane 1: Cars going right }
  VehRow[0] := 12; VehCol[0] := 5;  VehSpd[0] := 1; VehDir[0] := 1;  VehW[0] := 4;
  VehRow[1] := 12; VehCol[1] := 35; VehSpd[1] := 1; VehDir[1] := 1;  VehW[1] := 4;

  { Lane 2: Trucks going left }
  VehRow[2] := 13; VehCol[2] := 15; VehSpd[2] := 1; VehDir[2] := -1; VehW[2] := 6;
  VehRow[3] := 13; VehCol[3] := 50; VehSpd[3] := 1; VehDir[3] := -1; VehW[3] := 6;

  { Lane 3: Cars going right }
  VehRow[4] := 14; VehCol[4] := 10; VehSpd[4] := 2; VehDir[4] := 1;  VehW[4] := 4;
  VehRow[5] := 14; VehCol[5] := 45; VehSpd[5] := 2; VehDir[5] := 1;  VehW[5] := 4;

  { Lane 4: Trucks going left }
  VehRow[6] := 15; VehCol[6] := 20; VehSpd[6] := 1; VehDir[6] := -1; VehW[6] := 6;

  { Lane 5: Fast cars going right }
  VehRow[7] := 16; VehCol[7] := 8;  VehSpd[7] := 2; VehDir[7] := 1;  VehW[7] := 4;
  VehRow[8] := 16; VehCol[8] := 40; VehSpd[8] := 2; VehDir[8] := 1;  VehW[8] := 4;
  VehRow[9] := 16; VehCol[9] := 60; VehSpd[9] := 2; VehDir[9] := 1;  VehW[9] := 4;

  { Initialize platforms using arrays }
  { Row 1: Logs going right }
  PlatRow[0] := 4; PlatCol[0] := 5;  PlatSpd[0] := 1; PlatDir[0] := 1;  PlatW[0] := 8;  PlatIsTurtle[0] := 0;
  PlatRow[1] := 4; PlatCol[1] := 35; PlatSpd[1] := 1; PlatDir[1] := 1;  PlatW[1] := 8;  PlatIsTurtle[1] := 0;

  { Row 2: Turtles going left }
  PlatRow[2] := 5; PlatCol[2] := 15; PlatSpd[2] := 1; PlatDir[2] := -1; PlatW[2] := 5;  PlatIsTurtle[2] := 1;
  PlatRow[3] := 5; PlatCol[3] := 50; PlatSpd[3] := 1; PlatDir[3] := -1; PlatW[3] := 5;  PlatIsTurtle[3] := 1;

  { Row 3: Long log going right }
  PlatRow[4] := 6; PlatCol[4] := 10; PlatSpd[4] := 1; PlatDir[4] := 1;  PlatW[4] := 12; PlatIsTurtle[4] := 0;
  PlatRow[5] := 6; PlatCol[5] := 45; PlatSpd[5] := 1; PlatDir[5] := 1;  PlatW[5] := 10; PlatIsTurtle[5] := 0;

  { Row 4: Turtles going left }
  PlatRow[6] := 7; PlatCol[6] := 20; PlatSpd[6] := 1; PlatDir[6] := -1; PlatW[6] := 4;  PlatIsTurtle[6] := 1;
  PlatRow[7] := 7; PlatCol[7] := 55; PlatSpd[7] := 1; PlatDir[7] := -1; PlatW[7] := 4;  PlatIsTurtle[7] := 1;

  { Row 5: Logs going right }
  PlatRow[8] := 8; PlatCol[8] := 12; PlatSpd[8] := 2; PlatDir[8] := 1;  PlatW[8] := 7;  PlatIsTurtle[8] := 0;
  PlatRow[9] := 8; PlatCol[9] := 48; PlatSpd[9] := 2; PlatDir[9] := 1;  PlatW[9] := 7;  PlatIsTurtle[9] := 0;
end;

{====================================================================
 Draw Game Board - Using for loops with arrays!
 ====================================================================}
procedure DrawBoard;
var
  I, J, K: Integer;
  Sym: String;
  Color: Integer;
begin
  { Draw title and status }
  PrintColorAt(1, 25, COLOR_CYAN, '*** CLASSIC FROGGER ***');
  PrintColorAt(1, 2, COLOR_WHITE, 'Lives: ');
  GotoXY(9, 1);
  Write(FrogLives);
  PrintColorAt(1, 60, COLOR_YELLOW, 'Score: ');
  GotoXY(67, 1);
  Write(Score);

  { Draw homes row }
  for I := 1 to GAME_WIDTH do
    PrintColorAt(HOME_ROW, I, COLOR_BLUE, '~');

  { Draw each home using array }
  for I := 0 to NUM_HOMES - 1 do
  begin
    if HomeFilled[I] = 1 then
    begin
      PrintColorAt(HOME_ROW, HomeCol[I] - 1, COLOR_GREEN, '[');
      PrintColorAt(HOME_ROW, HomeCol[I], COLOR_GREEN, 'F');
      PrintColorAt(HOME_ROW, HomeCol[I] + 1, COLOR_GREEN, ']');
    end
    else
    begin
      PrintColorAt(HOME_ROW, HomeCol[I] - 1, COLOR_WHITE, '[');
      PrintColorAt(HOME_ROW, HomeCol[I], COLOR_WHITE, ' ');
      PrintColorAt(HOME_ROW, HomeCol[I] + 1, COLOR_WHITE, ']');
    end;
  end;

  { Draw river section }
  for I := RIVER_START to RIVER_END do
    for J := 1 to GAME_WIDTH do
      PrintColorAt(I, J, COLOR_BLUE, '~');

  { Draw platforms using array }
  for I := 0 to NUM_PLATFORMS - 1 do
  begin
    if PlatIsTurtle[I] = 1 then
    begin
      Sym := 'O';
      Color := COLOR_GREEN;
    end
    else
    begin
      Sym := '=';
      Color := COLOR_YELLOW;
    end;

    for J := 0 to PlatW[I] - 1 do
    begin
      K := PlatCol[I] + J;
      if (K >= 1) and (K <= GAME_WIDTH) then
        PrintColorAt(PlatRow[I], K, Color, Sym);
    end;
  end;

  { Draw median (safe zone) }
  for I := 1 to GAME_WIDTH do
    PrintColorAt(MEDIAN_ROW, I, COLOR_GREEN, '-');
  PrintColorAt(MEDIAN_ROW, 28, COLOR_GREEN, 'SAFE ZONE');

  { Draw road section }
  for I := ROAD_START to ROAD_END do
    for J := 1 to GAME_WIDTH do
      PrintColorAt(I, J, COLOR_WHITE, '.');

  { Draw vehicles using array }
  for I := 0 to NUM_VEHICLES - 1 do
  begin
    for J := 0 to VehW[I] - 1 do
    begin
      K := VehCol[I] + J;
      if (K >= 1) and (K <= GAME_WIDTH) then
        PrintColorAt(VehRow[I], K, COLOR_RED, '=');
    end;
  end;

  { Draw start area }
  for I := 1 to GAME_WIDTH do
    PrintColorAt(START_ROW, I, COLOR_GREEN, '-');
  PrintColorAt(START_ROW, 30, COLOR_GREEN, 'START');

  { Draw frog }
  PrintColorAt(FrogRow, FrogCol, COLOR_GREEN, '@');

  { Draw instructions }
  PrintAt(20, 1, 'WASD=Move  P=Pause  Q=Quit  Goal: Fill all 5 homes!');
end;

{====================================================================
 Check if frog is in river
 ====================================================================}
function IsInRiver(Row: Integer): Integer;
begin
  if (Row >= RIVER_START) and (Row <= RIVER_END) then
    Result := 1
  else
    Result := 0;
end;

{====================================================================
 Frog Dies
 ====================================================================}
procedure FrogDie;
begin
  FrogLives := FrogLives - 1;
  if FrogLives <= 0 then
    FrogAlive := 0
  else
  begin
    FrogRow := FrogStartRow;
    FrogCol := FrogStartCol;
  end;
end;

{====================================================================
 Update Game State - Using for loops with arrays!
 ====================================================================}
procedure UpdateGame;
var
  I: Integer;
  OnPlatform: Integer;
  FoundHome: Integer;
  PlatSpeed: Integer;
  NewCol: Integer;
begin
  { Move all vehicles using for loop }
  for I := 0 to NUM_VEHICLES - 1 do
  begin
    NewCol := VehCol[I] + (VehSpd[I] * VehDir[I]);
    if NewCol > 75 then
      NewCol := 1 - VehW[I];
    if NewCol < (0 - VehW[I]) then
      NewCol := 75;
    VehCol[I] := NewCol;
  end;

  { Move all platforms using for loop }
  for I := 0 to NUM_PLATFORMS - 1 do
  begin
    NewCol := PlatCol[I] + (PlatSpd[I] * PlatDir[I]);
    if NewCol > 75 then
      NewCol := 1 - PlatW[I];
    if NewCol < (0 - PlatW[I]) then
      NewCol := 75;
    PlatCol[I] := NewCol;
  end;

  { Check if frog is in river }
  if IsInRiver(FrogRow) = 1 then
  begin
    OnPlatform := 0;
    PlatSpeed := 0;

    { Check each platform using for loop }
    for I := 0 to NUM_PLATFORMS - 1 do
    begin
      if CheckCollision(PlatRow[I], PlatCol[I], PlatW[I], FrogRow, FrogCol) = 1 then
      begin
        OnPlatform := 1;
        PlatSpeed := PlatSpd[I] * PlatDir[I];
      end;
    end;

    { Move frog with platform if on one }
    if OnPlatform = 1 then
    begin
      FrogCol := FrogCol + PlatSpeed;
      if FrogCol < 1 then
        FrogCol := 1;
      if FrogCol > GAME_WIDTH then
        FrogCol := GAME_WIDTH;
    end;

    { If not on platform, frog drowns }
    if OnPlatform = 0 then
    begin
      FrogDie;
      if FrogAlive = 1 then
      begin
        PrintColorAt(12, 28, COLOR_RED, 'SPLASH!');
        Delay(800);
      end;
      Exit;
    end;
  end;

  { Check road collisions using for loop }
  if (FrogRow >= ROAD_START) and (FrogRow <= ROAD_END) then
  begin
    for I := 0 to NUM_VEHICLES - 1 do
    begin
      if CheckCollision(VehRow[I], VehCol[I], VehW[I], FrogRow, FrogCol) = 1 then
      begin
        FrogDie;
        if FrogAlive = 1 then
        begin
          PrintColorAt(12, 28, COLOR_RED, 'SPLAT!');
          Delay(800);
        end;
        Exit;
      end;
    end;
  end;

  { Check if frog reached a home using for loop }
  if FrogRow = HOME_ROW then
  begin
    FoundHome := 0;

    for I := 0 to NUM_HOMES - 1 do
    begin
      if (FrogCol >= HomeCol[I] - 1) and (FrogCol <= HomeCol[I] + 1) then
      begin
        if HomeFilled[I] = 0 then
        begin
          HomeFilled[I] := 1;
          HomesFilledCount := HomesFilledCount + 1;
          Score := Score + 200;
          FoundHome := 1;
          if HomesFilledCount = 5 then
            GameRunning := 0
          else
          begin
            FrogRow := FrogStartRow;
            FrogCol := FrogStartCol;
            PrintColorAt(12, 25, COLOR_GREEN, 'HOME SAFE! +200');
            Delay(500);
          end;
        end
        else
          FrogDie;
      end;
    end;

    { If didn't land in a home slot, die }
    if FoundHome = 0 then
      FrogDie;
  end;

  { Check if frog went off screen }
  if (FrogCol < 1) or (FrogCol > GAME_WIDTH) then
    FrogDie;
end;

{====================================================================
 Handle Player Input
 ====================================================================}
procedure HandleInput;
var
  Key: String;
begin
  Key := InKey;

  if Length(Key) > 0 then
  begin
    if (Key = 'w') or (Key = 'W') then
    begin
      if FrogRow > 1 then
        FrogRow := FrogRow - 1;
    end;

    if (Key = 's') or (Key = 'S') then
    begin
      if FrogRow < 24 then
        FrogRow := FrogRow + 1;
    end;

    if (Key = 'a') or (Key = 'A') then
    begin
      if FrogCol > 1 then
        FrogCol := FrogCol - 1;
    end;

    if (Key = 'd') or (Key = 'D') then
    begin
      if FrogCol < GAME_WIDTH then
        FrogCol := FrogCol + 1;
    end;

    if (Key = 'q') or (Key = 'Q') then
      GameRunning := 0;

    if (Key = 'p') or (Key = 'P') then
    begin
      PrintColorAt(12, 25, COLOR_YELLOW, '*** PAUSED ***');
      PrintColorAt(13, 20, COLOR_WHITE, 'Press P to resume');
      repeat
        Key := InKey;
        Delay(50);
      until (Key = 'p') or (Key = 'P');
    end;
  end;
end;

{====================================================================
 Game Loop
 ====================================================================}
procedure GameLoop;
begin
  AltScreen(1);  { Enable alt screen - triggers batch mode for smooth rendering }
  ClrScr;
  HideCursor;

  while (GameRunning = 1) and (FrogAlive = 1) do
  begin
    DrawBoard;
    HandleInput;
    UpdateGame;
    Delay(100);
  end;

  ShowCursor;
  AltScreen(0);  { Exit alt screen - back to normal terminal }
  ClrScr;

  WriteLn;
  if HomesFilledCount = 5 then
  begin
    WriteLn('========================================');
    WriteLn('    CONGRATULATIONS! YOU WIN!');
    WriteLn('========================================');
    WriteLn;
    WriteLn('  You successfully filled all 5 homes!');
    WriteLn;
  end
  else if FrogAlive = 0 then
  begin
    WriteLn('========================================');
    WriteLn('         G A M E   O V E R');
    WriteLn('========================================');
    WriteLn;
  end
  else
  begin
    WriteLn('  Thanks for playing!');
    WriteLn;
  end;

  WriteLn('  Final Score: ', Score);
  WriteLn('  Homes Filled: ', HomesFilledCount, ' / 5');
  WriteLn;
  WriteLn('  Press any key to continue...');

  repeat
    Delay(50);
  until Length(InKey) > 0;
end;

{====================================================================
 Show Instructions
 ====================================================================}
procedure ShowInstructions;
var
  Key: String;
begin
  ClrScr;
  WriteLn;
  WriteLn('========================================');
  WriteLn('           HOW TO PLAY');
  WriteLn('========================================');
  WriteLn;
  WriteLn('  OBJECTIVE:');
  WriteLn('  Guide your frog safely across the road and river');
  WriteLn('  to fill all 5 homes at the top of the screen.');
  WriteLn;
  WriteLn('  CONTROLS:');
  WriteLn('    W - Move Up');
  WriteLn('    S - Move Down');
  WriteLn('    A - Move Left');
  WriteLn('    D - Move Right');
  WriteLn('    P - Pause Game');
  WriteLn('    Q - Quit to Menu');
  WriteLn;
  WriteLn('  GAMEPLAY:');
  WriteLn('  * Avoid cars and trucks on the road');
  WriteLn('  * Jump on logs and turtles in the river');
  WriteLn('  * You drown if you fall in the water!');
  WriteLn('  * Land in the home slots [ ] at the top');
  WriteLn('  * Each home filled = +200 points');
  WriteLn('  * You have 3 lives - use them wisely!');
  WriteLn;
  WriteLn('  Press any key to return to menu...');

  repeat
    Key := InKey;
    Delay(50);
  until Length(Key) > 0;
end;

{====================================================================
 Main Menu
 ====================================================================}
procedure ShowMainMenu;
var
  Choice: Integer;
  Key: String;
begin
  Choice := 1;

  while True do
  begin
    ClrScr;
    WriteLn;
    WriteLn('========================================');
    WriteLn('          CLASSIC FROGGER');
    WriteLn('          (Pascal Version)');
    WriteLn('========================================');
    WriteLn;

    if Choice = 1 then
      WriteLn('          > START GAME')
    else
      WriteLn('            START GAME');

    if Choice = 2 then
      WriteLn('          > INSTRUCTIONS')
    else
      WriteLn('            INSTRUCTIONS');

    if Choice = 3 then
      WriteLn('          > QUIT')
    else
      WriteLn('            QUIT');

    WriteLn;
    WriteLn('     Use W/S to navigate, ENTER to select');
    WriteLn;

    Key := InKey;
    if Length(Key) > 0 then
    begin
      if (Key = 'w') or (Key = 'W') then
      begin
        Choice := Choice - 1;
        if Choice < 1 then
          Choice := 3;
      end;

      if (Key = 's') or (Key = 'S') then
      begin
        Choice := Choice + 1;
        if Choice > 3 then
          Choice := 1;
      end;

      if (Key = Chr(13)) or (Key = Chr(10)) then
      begin
        Delay(200);

        if Choice = 1 then
        begin
          InitGame;
          GameLoop;
          Delay(200);
        end;

        if Choice = 2 then
        begin
          ShowInstructions;
          Delay(200);
        end;

        if Choice = 3 then
          Break;
      end;
    end;

    Delay(50);
  end;
end;

{====================================================================
 Main Program
 ====================================================================}
begin
  ShowMainMenu;

  ClrScr;
  WriteLn;
  WriteLn('Thanks for playing CLASSIC FROGGER!');
  WriteLn;
end.
