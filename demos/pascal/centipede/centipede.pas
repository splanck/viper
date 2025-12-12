{====================================================================
 CENTIPEDE - Classic Arcade Game for Viper Pascal
 ====================================================================
 A simplified implementation using proper arrays
 ====================================================================}

program Centipede;

uses Crt;

const
  FIELD_WIDTH = 60;
  FIELD_HEIGHT = 22;
  MAX_CENTIPEDE_LEN = 12;
  MAX_MUSHROOMS = 30;
  MAX_BULLETS = 5;

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
 Global Variables
 ====================================================================}
var
  { Player state }
  PlayerX, PlayerY: Integer;
  PlayerLives: Integer;

  { Centipede segments [x, y, direction] }
  CentX: array[MAX_CENTIPEDE_LEN] of Integer;
  CentY: array[MAX_CENTIPEDE_LEN] of Integer;
  CentDir: array[MAX_CENTIPEDE_LEN] of Integer;  { 1 = right, -1 = left }
  CentLen: Integer;
  CentAlive: array[MAX_CENTIPEDE_LEN] of Integer;

  { Mushrooms [x, y, hits] }
  MushX: array[MAX_MUSHROOMS] of Integer;
  MushY: array[MAX_MUSHROOMS] of Integer;
  MushHits: array[MAX_MUSHROOMS] of Integer;  { 0 = destroyed, 1-2 = hits remaining }
  NumMushrooms: Integer;

  { Bullets [x, y, active] }
  BulletX: array[MAX_BULLETS] of Integer;
  BulletY: array[MAX_BULLETS] of Integer;
  BulletActive: array[MAX_BULLETS] of Integer;

  { Game state }
  Score: Integer;
  Level: Integer;
  GameOver: Integer;
  Running: Integer;
  MoveCounter: Integer;

{====================================================================
 Check if position has a mushroom
 ====================================================================}
function HasMushroom(x, y: Integer): Integer;
var
  i: Integer;
begin
  Result := 0;
  i := 0;
  while i < NumMushrooms do
  begin
    if (MushX[i] = x) and (MushY[i] = y) and (MushHits[i] > 0) then
      Result := 1;
    i := i + 1;
  end;
end;

{====================================================================
 Spawn mushrooms randomly
 ====================================================================}
procedure SpawnMushrooms;
var
  i, x, y: Integer;
begin
  NumMushrooms := 15 + Level * 3;
  if NumMushrooms > MAX_MUSHROOMS then
    NumMushrooms := MAX_MUSHROOMS;

  i := 0;
  while i < NumMushrooms do
  begin
    x := 2 + Trunc(Random * (FIELD_WIDTH - 4));
    y := 2 + Trunc(Random * (FIELD_HEIGHT - 6));
    MushX[i] := x;
    MushY[i] := y;
    MushHits[i] := 2;
    i := i + 1;
  end;
end;

{====================================================================
 Initialize centipede
 ====================================================================}
procedure InitCentipede;
var
  i: Integer;
begin
  CentLen := 8 + Level * 2;
  if CentLen > MAX_CENTIPEDE_LEN then
    CentLen := MAX_CENTIPEDE_LEN;

  i := 0;
  while i < CentLen do
  begin
    CentX[i] := FIELD_WIDTH - 2 - i;
    CentY[i] := 1;
    CentDir[i] := -1;  { Start moving left }
    CentAlive[i] := 1;
    i := i + 1;
  end;
end;

{====================================================================
 Initialize game
 ====================================================================}
procedure InitGame;
var
  i: Integer;
begin
  PlayerX := FIELD_WIDTH div 2;
  PlayerY := FIELD_HEIGHT - 2;
  PlayerLives := 3;
  Score := 0;
  Level := 1;
  GameOver := 0;
  MoveCounter := 0;

  { Clear bullets }
  i := 0;
  while i < MAX_BULLETS do
  begin
    BulletActive[i] := 0;
    i := i + 1;
  end;

  SpawnMushrooms;
  InitCentipede;
end;

{====================================================================
 Fire a bullet
 ====================================================================}
procedure FireBullet;
var
  i: Integer;
begin
  i := 0;
  while i < MAX_BULLETS do
  begin
    if BulletActive[i] = 0 then
    begin
      BulletX[i] := PlayerX;
      BulletY[i] := PlayerY - 1;
      BulletActive[i] := 1;
      i := MAX_BULLETS;  { Exit loop }
    end
    else
      i := i + 1;
  end;
end;

{====================================================================
 Update bullets
 ====================================================================}
procedure UpdateBullets;
var
  i, j: Integer;
  hit: Integer;
begin
  i := 0;
  while i < MAX_BULLETS do
  begin
    if BulletActive[i] = 1 then
    begin
      BulletY[i] := BulletY[i] - 1;

      { Check if bullet went off screen }
      if BulletY[i] < 1 then
        BulletActive[i] := 0
      else
      begin
        { Check mushroom collision }
        j := 0;
        while j < NumMushrooms do
        begin
          if (MushX[j] = BulletX[i]) and (MushY[j] = BulletY[i]) and (MushHits[j] > 0) then
          begin
            MushHits[j] := MushHits[j] - 1;
            BulletActive[i] := 0;
            if MushHits[j] = 0 then
              Score := Score + 1;
          end;
          j := j + 1;
        end;

        { Check centipede collision }
        if BulletActive[i] = 1 then
        begin
          j := 0;
          while j < CentLen do
          begin
            if (CentAlive[j] = 1) and (CentX[j] = BulletX[i]) and (CentY[j] = BulletY[i]) then
            begin
              CentAlive[j] := 0;
              BulletActive[i] := 0;
              Score := Score + 10;
              { Create mushroom where segment died }
              if NumMushrooms < MAX_MUSHROOMS then
              begin
                MushX[NumMushrooms] := CentX[j];
                MushY[NumMushrooms] := CentY[j];
                MushHits[NumMushrooms] := 2;
                NumMushrooms := NumMushrooms + 1;
              end;
            end;
            j := j + 1;
          end;
        end;
      end;
    end;
    i := i + 1;
  end;
end;

{====================================================================
 Check if all centipede segments are dead
 ====================================================================}
function CentipedeAllDead: Integer;
var
  i: Integer;
begin
  Result := 1;
  i := 0;
  while i < CentLen do
  begin
    if CentAlive[i] = 1 then
      Result := 0;
    i := i + 1;
  end;
end;

{====================================================================
 Update centipede movement
 ====================================================================}
procedure UpdateCentipede;
var
  i: Integer;
  newX, newY: Integer;
  blocked: Integer;
begin
  i := 0;
  while i < CentLen do
  begin
    if CentAlive[i] = 1 then
    begin
      { Try to move horizontally }
      newX := CentX[i] + CentDir[i];
      newY := CentY[i];
      blocked := 0;

      { Check boundaries }
      if (newX < 1) or (newX >= FIELD_WIDTH - 1) then
        blocked := 1;

      { Check mushroom collision }
      if (blocked = 0) and (HasMushroom(newX, newY) = 1) then
        blocked := 1;

      if blocked = 1 then
      begin
        { Move down and reverse direction }
        CentDir[i] := -CentDir[i];
        CentY[i] := CentY[i] + 1;

        { Check if reached player zone }
        if CentY[i] >= FIELD_HEIGHT - 1 then
          CentY[i] := 1;  { Wrap to top }
      end
      else
        CentX[i] := newX;

      { Check collision with player }
      if (CentX[i] = PlayerX) and (CentY[i] = PlayerY) then
      begin
        PlayerLives := PlayerLives - 1;
        if PlayerLives <= 0 then
          GameOver := 1
        else
        begin
          { Reset player position }
          PlayerX := FIELD_WIDTH div 2;
          PlayerY := FIELD_HEIGHT - 2;
        end;
      end;
    end;
    i := i + 1;
  end;
end;

{====================================================================
 Draw the game
 ====================================================================}
procedure DrawGame;
var
  i: Integer;
begin
  ClrScr;

  { Draw border }
  GotoXY(1, 1);
  TextColor(COLOR_WHITE);
  i := 0;
  while i < FIELD_WIDTH do
  begin
    Write('=');
    i := i + 1;
  end;

  i := 2;
  while i <= FIELD_HEIGHT do
  begin
    GotoXY(1, i);
    Write('|');
    GotoXY(FIELD_WIDTH, i);
    Write('|');
    i := i + 1;
  end;

  GotoXY(1, FIELD_HEIGHT + 1);
  i := 0;
  while i < FIELD_WIDTH do
  begin
    Write('=');
    i := i + 1;
  end;

  { Draw mushrooms }
  TextColor(COLOR_GREEN);
  i := 0;
  while i < NumMushrooms do
  begin
    if MushHits[i] > 0 then
    begin
      GotoXY(MushX[i], MushY[i]);
      if MushHits[i] = 2 then
        Write('M')
      else
        Write('m');
    end;
    i := i + 1;
  end;

  { Draw centipede }
  i := 0;
  while i < CentLen do
  begin
    if CentAlive[i] = 1 then
    begin
      if i = 0 then
      begin
        TextColor(COLOR_YELLOW);
        GotoXY(CentX[i], CentY[i]);
        Write('@');
      end
      else
      begin
        TextColor(COLOR_RED);
        GotoXY(CentX[i], CentY[i]);
        Write('O');
      end;
    end;
    i := i + 1;
  end;

  { Draw bullets }
  TextColor(COLOR_CYAN);
  i := 0;
  while i < MAX_BULLETS do
  begin
    if BulletActive[i] = 1 then
    begin
      GotoXY(BulletX[i], BulletY[i]);
      Write('^');
    end;
    i := i + 1;
  end;

  { Draw player }
  TextColor(COLOR_WHITE);
  GotoXY(PlayerX, PlayerY);
  Write('A');

  { Draw status }
  GotoXY(2, FIELD_HEIGHT + 2);
  TextColor(COLOR_CYAN);
  Write('Score: ');
  TextColor(COLOR_WHITE);
  Write(Score);
  Write('  ');
  TextColor(COLOR_GREEN);
  Write('Lives: ');
  TextColor(COLOR_WHITE);
  Write(PlayerLives);
  Write('  ');
  TextColor(COLOR_YELLOW);
  Write('Level: ');
  TextColor(COLOR_WHITE);
  Write(Level);
  Write('  [A/D]=Move [SPACE]=Shoot [Q]=Quit');
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

    { Handle input }
    Key := InKey;
    if Length(Key) > 0 then
    begin
      if (Key = 'a') or (Key = 'A') then
      begin
        if PlayerX > 2 then
          PlayerX := PlayerX - 1;
      end
      else if (Key = 'd') or (Key = 'D') then
      begin
        if PlayerX < FIELD_WIDTH - 2 then
          PlayerX := PlayerX + 1;
      end
      else if (Key = 'w') or (Key = 'W') then
      begin
        if PlayerY > FIELD_HEIGHT - 4 then
          PlayerY := PlayerY - 1;
      end
      else if (Key = 's') or (Key = 'S') then
      begin
        if PlayerY < FIELD_HEIGHT - 1 then
          PlayerY := PlayerY + 1;
      end
      else if Key = ' ' then
        FireBullet
      else if (Key = 'q') or (Key = 'Q') then
        GameOver := 1;
    end;

    { Update game state }
    UpdateBullets;

    MoveCounter := MoveCounter + 1;
    if MoveCounter >= 3 then
    begin
      MoveCounter := 0;
      UpdateCentipede;
    end;

    { Check for level complete }
    if CentipedeAllDead = 1 then
    begin
      Level := Level + 1;
      Score := Score + 100;
      SpawnMushrooms;
      InitCentipede;
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
  TextColor(COLOR_GREEN);
  GotoXY(15, 5);
  WriteLn('+=================================+');
  GotoXY(15, 6);
  WriteLn('|                                 |');
  GotoXY(15, 7);
  Write('|');
  TextColor(COLOR_YELLOW);
  Write('        C E N T I P E D E        ');
  TextColor(COLOR_GREEN);
  WriteLn('|');
  GotoXY(15, 8);
  WriteLn('|                                 |');
  GotoXY(15, 9);
  WriteLn('+=================================+');

  TextColor(COLOR_YELLOW);
  GotoXY(22, 11);
  WriteLn('  <@@@@@@@@@@>');
  TextColor(COLOR_CYAN);
  GotoXY(22, 12);
  WriteLn('  /|||||||||||\');

  TextColor(COLOR_WHITE);
  GotoXY(22, 15);
  WriteLn('[1] NEW GAME');
  GotoXY(22, 17);
  WriteLn('[Q] QUIT');

  TextColor(COLOR_CYAN);
  GotoXY(15, 20);
  WriteLn('    Viper Pascal Demo 2025');

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
  TextColor(COLOR_YELLOW);
  Write('Level Reached: ');
  TextColor(COLOR_WHITE);
  WriteLn(Level);

  GotoXY(15, 16);
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
  WriteLn('Thanks for playing CENTIPEDE!');
  GotoXY(15, 13);
  TextColor(COLOR_WHITE);
  WriteLn('A Viper Pascal Demo');
  GotoXY(1, 15);
end.
