{
  CENTIPEDE - Pascal Version (Simplified)

  A simplified Centipede clone using only parallel arrays due to Pascal frontend limitations.
  Note: Record field access and classes are not supported in current Pascal frontend.
}
program Centipede;

uses Crt;

const
  { Field dimensions }
  FIELD_WIDTH = 40;
  FIELD_HEIGHT = 24;
  FIELD_TOP = 2;
  FIELD_LEFT = 20;
  PLAYER_ZONE = 4;

  { Game constants }
  MAX_SEGMENTS = 12;
  SPIDER_POINTS = 50;
  SEGMENT_POINTS = 10;
  MAX_MUSHROOMS = 960;  { 40 x 24 }

var
  { Mushroom field - each cell stores health (0=empty, 1-4=health) }
  Mushrooms: array[960] of Integer;  { 40 x 24 }
  MushroomCount: Integer;

  { Player data }
  PlayerX, PlayerY: Integer;
  PlayerLives, PlayerScore: Integer;
  BulletX, BulletY, BulletActive: Integer;

  { Centipede segments - parallel arrays }
  SegX: array[12] of Integer;
  SegY: array[12] of Integer;
  SegDirX: array[12] of Integer;
  SegActive: array[12] of Integer;
  SegIsHead: array[12] of Integer;
  SegmentCount: Integer;
  CentiMoveTimer, CentiSpeed: Integer;

  { Spider data }
  SpiderX, SpiderY: Integer;
  SpiderDirX, SpiderDirY: Integer;
  SpiderActive, SpiderMoveTimer: Integer;
  SpiderSpawnTimer: Integer;

  { Game state }
  CurrentLevel: Integer;
  GameRunning: Integer;

{ Helper: Random integer from 0 to max-1 }
function RandInt(max: Integer): Integer;
begin
  Result := Trunc(Random * max);
end;

{ Get mushroom health at position }
function GetMushroom(x, y: Integer): Integer;
begin
  if (x < 0) or (x >= FIELD_WIDTH) or (y < 0) or (y >= FIELD_HEIGHT) then
    Result := 0
  else
    Result := Mushrooms[y * FIELD_WIDTH + x];
end;

{ Set mushroom health at position }
procedure SetMushroom(x, y, health: Integer);
var
  idx, oldHealth: Integer;
begin
  if (x >= 0) and (x < FIELD_WIDTH) and (y >= 0) and (y < FIELD_HEIGHT) then
  begin
    idx := y * FIELD_WIDTH + x;
    oldHealth := Mushrooms[idx];
    Mushrooms[idx] := health;

    if (oldHealth = 0) and (health > 0) then
      MushroomCount := MushroomCount + 1
    else if (oldHealth > 0) and (health = 0) then
      MushroomCount := MushroomCount - 1;
  end;
end;

{ Damage mushroom, returns 1 if destroyed }
function DamageMushroom(x, y: Integer): Integer;
var
  health: Integer;
begin
  health := GetMushroom(x, y);
  if health > 0 then
  begin
    health := health - 1;
    SetMushroom(x, y, health);
    if health = 0 then
      Result := 1
    else
      Result := 0;
  end
  else
    Result := 0;
end;

{ Draw single mushroom }
procedure DrawMushroom(x, y: Integer);
var
  health, screenX, screenY: Integer;
begin
  health := GetMushroom(x, y);
  screenX := FIELD_LEFT + x;
  screenY := FIELD_TOP + y;

  GotoXY(screenX, screenY);

  if health = 0 then
  begin
    TextColor(0);
    Write(' ');
  end
  else if health = 4 then
  begin
    TextColor(10);
    Write('@');
  end
  else if health = 3 then
  begin
    TextColor(2);
    Write('@');
  end
  else if health = 2 then
  begin
    TextColor(3);
    Write('o');
  end
  else
  begin
    TextColor(8);
    Write('.');
  end;
end;

{ Clear position on screen }
procedure ClearPosition(x, y: Integer);
var
  screenX, screenY, health: Integer;
begin
  screenX := FIELD_LEFT + x;
  screenY := FIELD_TOP + y;

  health := GetMushroom(x, y);
  if health > 0 then
    DrawMushroom(x, y)
  else
  begin
    GotoXY(screenX, screenY);
    TextColor(0);
    Write(' ');
  end;
end;

{ Generate mushrooms for level }
procedure GenerateMushrooms(level: Integer);
var
  count, placed, attempts, x, y, i: Integer;
begin
  { Clear all mushrooms }
  for i := 0 to MAX_MUSHROOMS - 1 do
    Mushrooms[i] := 0;
  MushroomCount := 0;

  { More mushrooms at higher levels }
  count := 20 + level * 5;
  if count > 60 then
    count := 60;

  placed := 0;
  attempts := 0;

  while (placed < count) and (attempts < 500) do
  begin
    x := RandInt(FIELD_WIDTH);
    y := RandInt(FIELD_HEIGHT - PLAYER_ZONE - 1);

    if GetMushroom(x, y) = 0 then
    begin
      SetMushroom(x, y, 4);
      placed := placed + 1;
    end;
    attempts := attempts + 1;
  end;
end;

{ Draw field border and mushrooms }
procedure DrawField;
var
  fx, fy: Integer;
begin
  TextColor(8);
  for fy := FIELD_TOP - 1 to FIELD_TOP + FIELD_HEIGHT do
  begin
    GotoXY(FIELD_LEFT - 1, fy);
    Write('|');
    GotoXY(FIELD_LEFT + FIELD_WIDTH, fy);
    Write('|');
  end;

  for fy := 0 to FIELD_HEIGHT - 1 do
    for fx := 0 to FIELD_WIDTH - 1 do
      DrawMushroom(fx, fy);
end;

{ Initialize player }
procedure InitPlayer;
begin
  PlayerX := FIELD_WIDTH div 2;
  PlayerY := FIELD_HEIGHT - 2;
  PlayerLives := 3;
  PlayerScore := 0;
  BulletActive := 0;
end;

{ Reset player position }
procedure ResetPlayer;
begin
  PlayerX := FIELD_WIDTH div 2;
  PlayerY := FIELD_HEIGHT - 2;
  BulletActive := 0;
end;

{ Draw player }
procedure DrawPlayer;
var
  screenX, screenY: Integer;
begin
  screenX := FIELD_LEFT + PlayerX;
  screenY := FIELD_TOP + PlayerY;
  GotoXY(screenX, screenY);
  TextColor(15);
  Write('A');
end;

{ Draw bullet }
procedure DrawBullet;
var
  screenX, screenY: Integer;
begin
  if BulletActive = 1 then
  begin
    screenX := FIELD_LEFT + BulletX;
    screenY := FIELD_TOP + BulletY;
    GotoXY(screenX, screenY);
    TextColor(14);
    Write('|');
  end;
end;

{ Draw HUD }
procedure DrawHUD;
var
  i: Integer;
begin
  GotoXY(FIELD_LEFT, 1);
  TextColor(11);
  Write('SCORE: ');
  TextColor(15);
  Write(PlayerScore);

  GotoXY(FIELD_LEFT + 20, 1);
  TextColor(12);
  Write('LIVES: ');
  TextColor(15);
  for i := 1 to PlayerLives do
    Write('A ');
  Write('   ');
end;

{ Initialize centipede for level }
procedure InitCentipede(level, startX: Integer);
var
  i, centiLength: Integer;
begin
  centiLength := 8 + level;
  if centiLength > MAX_SEGMENTS then
    centiLength := MAX_SEGMENTS;

  SegmentCount := centiLength;
  CentiSpeed := 4 - level div 3;
  if CentiSpeed < 1 then
    CentiSpeed := 1;
  CentiMoveTimer := 0;

  for i := 0 to centiLength - 1 do
  begin
    SegX[i] := startX - i;
    SegY[i] := 0;
    SegDirX[i] := 1;
    SegActive[i] := 1;
    if i = 0 then
      SegIsHead[i] := 1
    else
      SegIsHead[i] := 0;
  end;
end;

{ Count active centipede segments }
function ActiveSegmentCount: Integer;
var
  i, count: Integer;
begin
  count := 0;
  for i := 0 to SegmentCount - 1 do
    if SegActive[i] = 1 then
      count := count + 1;
  Result := count;
end;

{ Move centipede, returns 1 if reached bottom }
function MoveCentipede: Integer;
var
  i, newX, blocked, reachedBottom: Integer;
begin
  CentiMoveTimer := CentiMoveTimer + 1;
  if CentiMoveTimer < CentiSpeed then
  begin
    Result := 0;
  end
  else
  begin
    CentiMoveTimer := 0;
    reachedBottom := 0;

    for i := 0 to SegmentCount - 1 do
    begin
      if SegActive[i] = 1 then
      begin
        newX := SegX[i] + SegDirX[i];
        blocked := 0;

        if (newX < 0) or (newX >= FIELD_WIDTH) then
          blocked := 1
        else if GetMushroom(newX, SegY[i]) > 0 then
          blocked := 1;

        if blocked = 1 then
        begin
          SegY[i] := SegY[i] + 1;
          SegDirX[i] := -SegDirX[i];
          if SegY[i] >= FIELD_HEIGHT then
            reachedBottom := 1;
        end
        else
          SegX[i] := newX;
      end;
    end;

    Result := reachedBottom;
  end;
end;

{ Find segment at position, returns index or -1 }
function SegmentAt(x, y: Integer): Integer;
var
  i: Integer;
begin
  for i := 0 to SegmentCount - 1 do
  begin
    if SegActive[i] = 1 then
      if (SegX[i] = x) and (SegY[i] = y) then
      begin
        Result := i;
      end;
  end;
  Result := -1;
end;

{ Kill segment, returns points }
function KillSegment(idx: Integer): Integer;
begin
  if (idx >= 0) and (idx < SegmentCount) then
  begin
    if SegActive[idx] = 1 then
    begin
      SegActive[idx] := 0;
      SetMushroom(SegX[idx], SegY[idx], 4);
      if idx < SegmentCount - 1 then
        SegIsHead[idx + 1] := 1;
      Result := SEGMENT_POINTS;
    end
    else
      Result := 0;
  end
  else
    Result := 0;
end;

{ Draw centipede }
procedure DrawCentipede;
var
  i, screenX, screenY: Integer;
begin
  for i := 0 to SegmentCount - 1 do
  begin
    if SegActive[i] = 1 then
    begin
      screenX := FIELD_LEFT + SegX[i];
      screenY := FIELD_TOP + SegY[i];
      GotoXY(screenX, screenY);
      if SegIsHead[i] = 1 then
      begin
        TextColor(12);
        Write('O');
      end
      else
      begin
        TextColor(10);
        Write('o');
      end;
    end;
  end;
end;

{ Clear centipede from screen }
procedure ClearCentipede;
var
  i: Integer;
begin
  for i := 0 to SegmentCount - 1 do
    if SegActive[i] = 1 then
      ClearPosition(SegX[i], SegY[i]);
end;

{ Initialize spider }
procedure InitSpider;
begin
  SpiderX := 0;
  SpiderY := 0;
  SpiderDirX := 1;
  SpiderDirY := 1;
  SpiderActive := 0;
  SpiderMoveTimer := 0;
end;

{ Spawn spider }
procedure SpawnSpider;
begin
  SpiderY := FIELD_HEIGHT - RandInt(PLAYER_ZONE) - 1;
  if RandInt(2) = 0 then
  begin
    SpiderX := 0;
    SpiderDirX := 1;
  end
  else
  begin
    SpiderX := FIELD_WIDTH - 1;
    SpiderDirX := -1;
  end;
  SpiderDirY := 1;
  if RandInt(2) = 0 then
    SpiderDirY := -1;
  SpiderActive := 1;
  SpiderMoveTimer := 0;
end;

{ Move spider }
procedure MoveSpider;
begin
  if SpiderActive = 0 then
  begin
  end
  else
  begin
    SpiderMoveTimer := SpiderMoveTimer + 1;
    if SpiderMoveTimer >= 2 then
    begin
      SpiderMoveTimer := 0;

      SpiderX := SpiderX + SpiderDirX;
      if RandInt(3) = 0 then
        SpiderY := SpiderY + SpiderDirY;

      if RandInt(5) = 0 then
        SpiderDirY := -SpiderDirY;

      if SpiderY < FIELD_HEIGHT - PLAYER_ZONE then
      begin
        SpiderY := FIELD_HEIGHT - PLAYER_ZONE;
        SpiderDirY := 1;
      end;
      if SpiderY >= FIELD_HEIGHT then
      begin
        SpiderY := FIELD_HEIGHT - 1;
        SpiderDirY := -1;
      end;

      if (SpiderX < 0) or (SpiderX >= FIELD_WIDTH) then
        SpiderActive := 0;

      if GetMushroom(SpiderX, SpiderY) > 0 then
        SetMushroom(SpiderX, SpiderY, 0);
    end;
  end;
end;

{ Check if spider at position }
function SpiderIsAt(x, y: Integer): Integer;
begin
  if (SpiderActive = 1) and (SpiderX = x) and (SpiderY = y) then
    Result := 1
  else
    Result := 0;
end;

{ Draw spider }
procedure DrawSpider;
var
  screenX, screenY: Integer;
begin
  if SpiderActive = 1 then
  begin
    screenX := FIELD_LEFT + SpiderX;
    screenY := FIELD_TOP + SpiderY;
    GotoXY(screenX, screenY);
    TextColor(13);
    Write('X');
  end;
end;

{ Clear spider }
procedure ClearSpider;
begin
  if SpiderActive = 1 then
    ClearPosition(SpiderX, SpiderY);
end;

{ Initialize level }
procedure InitLevel(level: Integer);
begin
  GenerateMushrooms(level);
  InitCentipede(level, FIELD_WIDTH div 2);
  InitSpider;
  SpiderSpawnTimer := 100;
  CurrentLevel := level;
end;

{ Draw game screen }
procedure DrawGameScreen;
begin
  ClrScr;
  DrawHUD;
  DrawField;

  GotoXY(FIELD_LEFT + 35, 1);
  TextColor(14);
  Write('LVL:');
  TextColor(15);
  Write(CurrentLevel);
end;

{ Show game over }
procedure ShowGameOver;
begin
  GotoXY(FIELD_LEFT + 10, 12);
  TextColor(12);
  Write('*** GAME OVER ***');

  GotoXY(FIELD_LEFT + 8, 14);
  TextColor(11);
  Write('Final Score: ');
  TextColor(15);
  Write(PlayerScore);

  GotoXY(FIELD_LEFT + 5, 16);
  TextColor(8);
  Write('Press any key...');
  ReadKey;
end;

{ Show level complete }
procedure ShowLevelComplete;
begin
  GotoXY(FIELD_LEFT + 8, 12);
  TextColor(10);
  Write('*** LEVEL COMPLETE ***');

  GotoXY(FIELD_LEFT + 10, 14);
  TextColor(11);
  Write('Score: ');
  TextColor(15);
  Write(PlayerScore);

  Delay(1500);
end;

{ Show main menu }
procedure ShowMainMenu;
begin
  ClrScr;

  TextColor(10);
  GotoXY(15, 3);
  Write('+=================================+');
  GotoXY(15, 4);
  Write('|                                 |');
  GotoXY(15, 5);
  Write('|');
  TextColor(11);
  Write('       C E N T I P E D E       ');
  TextColor(10);
  Write('|');
  GotoXY(15, 6);
  Write('|                                 |');
  GotoXY(15, 7);
  Write('+=================================+');

  TextColor(14);
  GotoXY(22, 9);
  Write('  <@@@@@@@@@@>');
  TextColor(6);
  GotoXY(22, 10);
  Write('  /||||||||||\');

  GotoXY(22, 13);
  TextColor(15);
  Write('[1] ');
  TextColor(10);
  Write('NEW GAME');

  GotoXY(22, 15);
  TextColor(15);
  Write('[Q] ');
  TextColor(12);
  Write('QUIT');

  TextColor(8);
  GotoXY(17, 18);
  Write('     Viper Pascal Demo 2024     ');

  TextColor(7);
end;

{ Main game loop }
procedure PlayGame;
var
  key: String;
  oldX, oldY, oldBulletY: Integer;
  hitSegment, points: Integer;
begin
  InitPlayer;
  InitLevel(1);
  DrawGameScreen;
  GameRunning := 1;

  while GameRunning = 1 do
  begin
    key := InKey;

    oldX := PlayerX;
    oldY := PlayerY;

    if (key = 'w') or (key = 'W') then
    begin
      if PlayerY > FIELD_HEIGHT - PLAYER_ZONE then
        PlayerY := PlayerY - 1;
    end
    else if (key = 's') or (key = 'S') then
    begin
      if PlayerY < FIELD_HEIGHT - 1 then
        PlayerY := PlayerY + 1;
    end
    else if (key = 'a') or (key = 'A') then
    begin
      if PlayerX > 0 then
        PlayerX := PlayerX - 1;
    end
    else if (key = 'd') or (key = 'D') then
    begin
      if PlayerX < FIELD_WIDTH - 1 then
        PlayerX := PlayerX + 1;
    end
    else if key = ' ' then
    begin
      if BulletActive = 0 then
      begin
        BulletX := PlayerX;
        BulletY := PlayerY - 1;
        BulletActive := 1;
      end;
    end
    else if (key = 'q') or (key = 'Q') then
      GameRunning := 0;

    { Redraw player if moved }
    if (oldX <> PlayerX) or (oldY <> PlayerY) then
    begin
      ClearPosition(oldX, oldY);
      DrawPlayer;
    end;

    { Update bullet }
    if BulletActive = 1 then
    begin
      oldBulletY := BulletY;
      ClearPosition(BulletX, BulletY);
      BulletY := BulletY - 1;
      if BulletY < 0 then
        BulletActive := 0;

      { Check collisions }
      if BulletActive = 1 then
      begin
        { Hit mushroom? }
        if GetMushroom(BulletX, BulletY) > 0 then
        begin
          if DamageMushroom(BulletX, BulletY) = 1 then
            PlayerScore := PlayerScore + 1;
          DrawMushroom(BulletX, BulletY);
          BulletActive := 0;
          DrawHUD;
        end;

        { Hit centipede? }
        if BulletActive = 1 then
        begin
          hitSegment := SegmentAt(BulletX, BulletY);
          if hitSegment >= 0 then
          begin
            points := KillSegment(hitSegment);
            PlayerScore := PlayerScore + points;
            BulletActive := 0;
            DrawHUD;
            DrawMushroom(BulletX, BulletY);
          end;
        end;

        { Hit spider? }
        if BulletActive = 1 then
        begin
          if SpiderIsAt(BulletX, BulletY) = 1 then
          begin
            SpiderActive := 0;
            PlayerScore := PlayerScore + SPIDER_POINTS;
            BulletActive := 0;
            DrawHUD;
          end;
        end;

        if BulletActive = 1 then
          DrawBullet;
      end;
    end;

    { Move centipede }
    ClearCentipede;
    if MoveCentipede = 1 then
    begin
      PlayerLives := PlayerLives - 1;
      if PlayerLives <= 0 then
      begin
        ShowGameOver;
        GameRunning := 0;
      end
      else
      begin
        InitCentipede(CurrentLevel, FIELD_WIDTH div 2);
        DrawHUD;
      end;
    end;
    DrawCentipede;

    { Check player collision with centipede }
    if SegmentAt(PlayerX, PlayerY) >= 0 then
    begin
      PlayerLives := PlayerLives - 1;
      if PlayerLives <= 0 then
      begin
        ShowGameOver;
        GameRunning := 0;
      end
      else
      begin
        InitCentipede(CurrentLevel, FIELD_WIDTH div 2);
        ResetPlayer;
        DrawHUD;
      end;
    end;

    { Spider logic }
    SpiderSpawnTimer := SpiderSpawnTimer - 1;
    if (SpiderActive = 0) and (SpiderSpawnTimer <= 0) then
    begin
      SpawnSpider;
      SpiderSpawnTimer := 150 + RandInt(100);
    end;

    if SpiderActive = 1 then
    begin
      ClearSpider;
      MoveSpider;

      if SpiderIsAt(PlayerX, PlayerY) = 1 then
      begin
        PlayerLives := PlayerLives - 1;
        if PlayerLives <= 0 then
        begin
          ShowGameOver;
          GameRunning := 0;
        end
        else
        begin
          SpiderActive := 0;
          ResetPlayer;
          DrawHUD;
        end;
      end;

      DrawSpider;
    end;

    { Check level complete }
    if ActiveSegmentCount = 0 then
    begin
      ShowLevelComplete;
      CurrentLevel := CurrentLevel + 1;
      InitLevel(CurrentLevel);
      DrawGameScreen;
    end;

    DrawPlayer;
    Delay(30);
  end;
end;

{ Main program }
var
  running: Integer;
  choice: String;
begin
  Randomize;
  HideCursor;
  running := 1;

  while running = 1 do
  begin
    ShowMainMenu;
    choice := ReadKey;

    if choice = '1' then
      PlayGame
    else if (choice = 'q') or (choice = 'Q') then
      running := 0;
  end;

  ClrScr;
  ShowCursor;
  GotoXY(1, 1);
  WriteLn('Thanks for playing CENTIPEDE!');
end.
