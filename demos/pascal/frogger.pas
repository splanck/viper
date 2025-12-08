{
  Frogger - Pascal Version (No Records)

  A simplified implementation of the classic Frogger arcade game.
  Uses parallel arrays instead of records due to Pascal frontend limitations.
}
program Frogger;

uses Crt;

const
  GAME_WIDTH = 60;
  GAME_HEIGHT = 20;

  { Row definitions }
  HOME_ROW = 2;
  RIVER_START = 4;
  RIVER_END = 7;
  MEDIAN_ROW = 9;
  ROAD_START = 11;
  ROAD_END = 14;
  START_ROW = 16;

  { Maximum entities }
  MAX_VEHICLES = 8;
  MAX_PLATFORMS = 6;
  MAX_HOMES = 5;

  { Color codes }
  COLOR_BLACK = 0;
  COLOR_RED = 1;
  COLOR_GREEN = 2;
  COLOR_YELLOW = 3;
  COLOR_BLUE = 4;
  COLOR_MAGENTA = 5;
  COLOR_CYAN = 6;
  COLOR_WHITE = 7;

var
  { Frog data }
  FrogRow: Integer;
  FrogCol: Integer;
  FrogLives: Integer;
  FrogStartRow: Integer;
  FrogStartCol: Integer;

  { Vehicle data - parallel arrays }
  VehicleRow: array[8] of Integer;
  VehicleCol: array[8] of Integer;
  VehicleSpeed: array[8] of Integer;
  VehicleDir: array[8] of Integer;
  VehicleWidth: array[8] of Integer;

  { Platform data - parallel arrays }
  PlatformRow: array[6] of Integer;
  PlatformCol: array[6] of Integer;
  PlatformSpeed: array[6] of Integer;
  PlatformDir: array[6] of Integer;
  PlatformWidth: array[6] of Integer;

  { Home data - parallel arrays }
  HomeCol: array[5] of Integer;
  HomeFilled: array[5] of Integer;

  Score: Integer;
  GameRunning: Integer;
  HomesFilledCount: Integer;
  FrameCount: Integer;

{ Initialize the frog }
procedure InitFrog;
begin
  FrogRow := START_ROW;
  FrogCol := GAME_WIDTH div 2;
  FrogLives := 3;
  FrogStartRow := START_ROW;
  FrogStartCol := GAME_WIDTH div 2;
end;

{ Reset frog to start position }
procedure ResetFrog;
begin
  FrogRow := FrogStartRow;
  FrogCol := FrogStartCol;
end;

{ Initialize vehicles }
procedure InitVehicles;
begin
  { Vehicle 0: Row 11, moving right }
  VehicleRow[0] := 11;
  VehicleCol[0] := 5;
  VehicleSpeed[0] := 1;
  VehicleDir[0] := 1;
  VehicleWidth[0] := 4;

  { Vehicle 1: Row 11, moving right }
  VehicleRow[1] := 11;
  VehicleCol[1] := 35;
  VehicleSpeed[1] := 1;
  VehicleDir[1] := 1;
  VehicleWidth[1] := 4;

  { Vehicle 2: Row 12, moving left }
  VehicleRow[2] := 12;
  VehicleCol[2] := 15;
  VehicleSpeed[2] := 1;
  VehicleDir[2] := -1;
  VehicleWidth[2] := 5;

  { Vehicle 3: Row 12, moving left }
  VehicleRow[3] := 12;
  VehicleCol[3] := 45;
  VehicleSpeed[3] := 1;
  VehicleDir[3] := -1;
  VehicleWidth[3] := 5;

  { Vehicle 4: Row 13, moving right fast }
  VehicleRow[4] := 13;
  VehicleCol[4] := 10;
  VehicleSpeed[4] := 2;
  VehicleDir[4] := 1;
  VehicleWidth[4] := 3;

  { Vehicle 5: Row 13, moving right fast }
  VehicleRow[5] := 13;
  VehicleCol[5] := 40;
  VehicleSpeed[5] := 2;
  VehicleDir[5] := 1;
  VehicleWidth[5] := 3;

  { Vehicle 6: Row 14, moving left }
  VehicleRow[6] := 14;
  VehicleCol[6] := 20;
  VehicleSpeed[6] := 1;
  VehicleDir[6] := -1;
  VehicleWidth[6] := 4;

  { Vehicle 7: Row 14, moving left }
  VehicleRow[7] := 14;
  VehicleCol[7] := 50;
  VehicleSpeed[7] := 1;
  VehicleDir[7] := -1;
  VehicleWidth[7] := 4;
end;

{ Initialize platforms }
procedure InitPlatforms;
begin
  { Platform 0: Row 4 }
  PlatformRow[0] := 4;
  PlatformCol[0] := 5;
  PlatformSpeed[0] := 1;
  PlatformDir[0] := 1;
  PlatformWidth[0] := 8;

  { Platform 1: Row 4 }
  PlatformRow[1] := 4;
  PlatformCol[1] := 35;
  PlatformSpeed[1] := 1;
  PlatformDir[1] := 1;
  PlatformWidth[1] := 6;

  { Platform 2: Row 5, moving left }
  PlatformRow[2] := 5;
  PlatformCol[2] := 15;
  PlatformSpeed[2] := 1;
  PlatformDir[2] := -1;
  PlatformWidth[2] := 5;

  { Platform 3: Row 5, moving left }
  PlatformRow[3] := 5;
  PlatformCol[3] := 45;
  PlatformSpeed[3] := 1;
  PlatformDir[3] := -1;
  PlatformWidth[3] := 5;

  { Platform 4: Row 6 }
  PlatformRow[4] := 6;
  PlatformCol[4] := 8;
  PlatformSpeed[4] := 1;
  PlatformDir[4] := 1;
  PlatformWidth[4] := 10;

  { Platform 5: Row 7, moving left }
  PlatformRow[5] := 7;
  PlatformCol[5] := 25;
  PlatformSpeed[5] := 1;
  PlatformDir[5] := -1;
  PlatformWidth[5] := 7;
end;

{ Initialize homes }
procedure InitHomes;
begin
  HomeCol[0] := 8;
  HomeCol[1] := 20;
  HomeCol[2] := 32;
  HomeCol[3] := 44;
  HomeCol[4] := 52;

  HomeFilled[0] := 0;
  HomeFilled[1] := 0;
  HomeFilled[2] := 0;
  HomeFilled[3] := 0;
  HomeFilled[4] := 0;
end;

{ Initialize the game }
procedure InitGame;
begin
  Score := 0;
  GameRunning := 1;
  HomesFilledCount := 0;
  FrameCount := 0;

  InitFrog;
  InitVehicles;
  InitPlatforms;
  InitHomes;
end;

{ Move vehicles }
procedure MoveVehicles;
var
  i: Integer;
  NewCol: Integer;
begin
  for i := 0 to 7 do
  begin
    NewCol := VehicleCol[i] + VehicleSpeed[i] * VehicleDir[i];
    if NewCol > GAME_WIDTH then
      NewCol := 1 - VehicleWidth[i]
    else if NewCol < 1 - VehicleWidth[i] then
      NewCol := GAME_WIDTH;
    VehicleCol[i] := NewCol;
  end;
end;

{ Move platforms }
procedure MovePlatforms;
var
  i: Integer;
  NewCol: Integer;
begin
  for i := 0 to 5 do
  begin
    NewCol := PlatformCol[i] + PlatformSpeed[i] * PlatformDir[i];
    if NewCol > GAME_WIDTH then
      NewCol := 1 - PlatformWidth[i]
    else if NewCol < 1 - PlatformWidth[i] then
      NewCol := GAME_WIDTH;
    PlatformCol[i] := NewCol;
  end;
end;

{ Check if frog collides with a specific vehicle }
function CheckVehicleHit(idx: Integer): Integer;
var
  j: Integer;
begin
  Result := 0;
  if FrogRow = VehicleRow[idx] then
  begin
    j := 0;
    while j < VehicleWidth[idx] do
    begin
      if FrogCol = VehicleCol[idx] + j then
        Result := 1;
      j := j + 1;
    end;
  end;
end;

{ Check if frog is on a specific platform }
function CheckOnPlatformIdx(idx: Integer): Integer;
var
  j: Integer;
begin
  Result := 0;
  if FrogRow = PlatformRow[idx] then
  begin
    j := 0;
    while j < PlatformWidth[idx] do
    begin
      if FrogCol = PlatformCol[idx] + j then
        Result := 1;
      j := j + 1;
    end;
  end;
end;

{ Frog dies }
procedure FrogDie;
begin
  FrogLives := FrogLives - 1;
  if FrogLives <= 0 then
    GameRunning := 0
  else
    ResetFrog;
end;

{ Draw the game board }
procedure DrawBoard;
var
  i, j: Integer;
begin
  ClrScr;

  { Draw title and status }
  GotoXY(1, 1);
  TextColor(COLOR_CYAN);
  WriteLn('*** FROGGER *** Lives: ', FrogLives, '  Score: ', Score);

  { Draw homes row }
  GotoXY(1, HOME_ROW);
  TextColor(COLOR_BLUE);
  for i := 1 to GAME_WIDTH do
    Write('~');

  { Draw homes }
  for i := 0 to 4 do
  begin
    GotoXY(HomeCol[i] - 1, HOME_ROW);
    if HomeFilled[i] = 1 then
    begin
      TextColor(COLOR_GREEN);
      Write('[F]');
    end
    else
    begin
      TextColor(COLOR_WHITE);
      Write('[ ]');
    end;
  end;

  { Draw river }
  TextColor(COLOR_BLUE);
  for i := RIVER_START to RIVER_END do
  begin
    GotoXY(1, i);
    for j := 1 to GAME_WIDTH do
      Write('~');
  end;

  { Draw platforms (logs) }
  TextColor(COLOR_YELLOW);
  for i := 0 to 5 do
  begin
    if (PlatformCol[i] >= 1) and (PlatformCol[i] <= GAME_WIDTH) then
    begin
      GotoXY(PlatformCol[i], PlatformRow[i]);
      for j := 0 to PlatformWidth[i] - 1 do
      begin
        if PlatformCol[i] + j <= GAME_WIDTH then
          Write('=');
      end;
    end;
  end;

  { Draw median (safe zone) }
  GotoXY(1, MEDIAN_ROW);
  TextColor(COLOR_GREEN);
  for i := 1 to GAME_WIDTH do
    Write('-');
  GotoXY(22, MEDIAN_ROW);
  Write('SAFE ZONE');

  { Draw road }
  TextColor(COLOR_WHITE);
  for i := ROAD_START to ROAD_END do
  begin
    GotoXY(1, i);
    for j := 1 to GAME_WIDTH do
      Write('.');
  end;

  { Draw vehicles }
  TextColor(COLOR_RED);
  for i := 0 to 7 do
  begin
    if (VehicleCol[i] >= 1) and (VehicleCol[i] <= GAME_WIDTH) then
    begin
      GotoXY(VehicleCol[i], VehicleRow[i]);
      for j := 0 to VehicleWidth[i] - 1 do
      begin
        if VehicleCol[i] + j <= GAME_WIDTH then
          Write('#');
      end;
    end;
  end;

  { Draw start area }
  GotoXY(1, START_ROW);
  TextColor(COLOR_GREEN);
  for i := 1 to GAME_WIDTH do
    Write('-');
  GotoXY(25, START_ROW);
  Write('START');

  { Draw frog }
  GotoXY(FrogCol, FrogRow);
  TextColor(COLOR_GREEN);
  Write('@');

  { Draw instructions }
  GotoXY(1, 18);
  TextColor(COLOR_WHITE);
  Write('WASD=Move  Q=Quit  Fill all 5 homes to win!');
end;

{ Check if frog is in river }
function IsInRiver: Integer;
begin
  if (FrogRow >= RIVER_START) and (FrogRow <= RIVER_END) then
    Result := 1
  else
    Result := 0;
end;

{ Update game state }
procedure UpdateGame;
var
  i: Integer;
  OnPlatform: Integer;
  PlatformSpeed2: Integer;
  FoundHome: Integer;
  HitVehicle: Integer;
begin
  { Move vehicles }
  MoveVehicles;

  { Move platforms }
  MovePlatforms;

  { Check river - must be on platform }
  if IsInRiver() = 1 then
  begin
    OnPlatform := 0;
    PlatformSpeed2 := 0;

    for i := 0 to 5 do
    begin
      if CheckOnPlatformIdx(i) = 1 then
      begin
        OnPlatform := 1;
        PlatformSpeed2 := PlatformSpeed[i] * PlatformDir[i];
      end;
    end;

    if OnPlatform = 1 then
    begin
      { Move with platform }
      FrogCol := FrogCol + PlatformSpeed2;
      if (FrogCol < 1) or (FrogCol > GAME_WIDTH) then
        FrogDie;
    end
    else
    begin
      { Drowns! }
      GotoXY(25, 10);
      TextColor(COLOR_RED);
      Write('SPLASH!');
      Delay(500);
      FrogDie;
    end;
  end;

  { Check road collisions }
  if (FrogRow >= ROAD_START) and (FrogRow <= ROAD_END) and (FrogLives > 0) then
  begin
    HitVehicle := 0;
    for i := 0 to 7 do
    begin
      if (HitVehicle = 0) and (CheckVehicleHit(i) = 1) then
      begin
        GotoXY(25, 10);
        TextColor(COLOR_RED);
        Write('SPLAT!');
        Delay(500);
        FrogDie;
        HitVehicle := 1;
      end;
    end;
  end;

  { Check if reached home }
  if (FrogRow = HOME_ROW) and (FrogLives > 0) then
  begin
    FoundHome := 0;

    for i := 0 to 4 do
    begin
      if (FrogCol >= HomeCol[i] - 1) and (FrogCol <= HomeCol[i] + 1) then
      begin
        if HomeFilled[i] = 0 then
        begin
          HomeFilled[i] := 1;
          HomesFilledCount := HomesFilledCount + 1;
          Score := Score + 200;
          FoundHome := 1;

          if HomesFilledCount = 5 then
            GameRunning := 0
          else
          begin
            GotoXY(20, 10);
            TextColor(COLOR_GREEN);
            Write('HOME SAFE! +200');
            Delay(500);
            ResetFrog;
          end;
        end
        else
        begin
          { Home already filled }
          FrogDie;
        end;
      end;
    end;

    if FoundHome = 0 then
      FrogDie;
  end;
end;

{ Handle player input }
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
      if FrogRow < GAME_HEIGHT then
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
  end;
end;

{ Main game loop }
procedure GameLoop;
begin
  ClrScr;
  HideCursor;

  while (GameRunning = 1) and (FrogLives > 0) do
  begin
    DrawBoard;
    HandleInput;
    UpdateGame;
    Delay(100);
    FrameCount := FrameCount + 1;
  end;

  { Game over screen }
  ClrScr;
  ShowCursor;

  if HomesFilledCount = MAX_HOMES then
  begin
    WriteLn;
    WriteLn('CONGRATULATIONS! YOU WIN!');
    WriteLn('You successfully filled all 5 homes!');
  end
  else if FrogLives <= 0 then
  begin
    WriteLn;
    WriteLn('GAME OVER');
    WriteLn('You ran out of lives!');
  end
  else
  begin
    WriteLn;
    WriteLn('Thanks for playing!');
  end;

  WriteLn;
  WriteLn('Final Score: ', Score);
  WriteLn('Homes Filled: ', HomesFilledCount, ' / 5');
  WriteLn;
  WriteLn('Press any key to exit...');
  ReadKey;
end;

{ Main program }
begin
  WriteLn('FROGGER - Pascal Edition');
  WriteLn;
  WriteLn('Controls:');
  WriteLn('  W - Move Up');
  WriteLn('  S - Move Down');
  WriteLn('  A - Move Left');
  WriteLn('  D - Move Right');
  WriteLn('  Q - Quit');
  WriteLn;
  WriteLn('Press any key to start...');
  ReadKey;

  InitGame;
  GameLoop;
end.
