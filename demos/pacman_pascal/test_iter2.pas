{ Pacman Pascal - Iteration 2: Terminal Crt APIs }
program TestIter2;

uses Crt;

var
    i: Integer;

begin
    WriteLn('=== Pacman Pascal Iteration 2 ===');
    WriteLn('Testing Crt terminal APIs...');
    WriteLn('Press any key after each test...');
    WriteLn('');

    { Test Clear }
    WriteLn('1. Testing ClrScr (Clear screen)...');

    ClrScr;

    { Test positioning }
    WriteLn('2. Testing GotoXY (positioning)...');
    GotoXY(10, 5);
    WriteLn('This text is at position (10, 5)');

    GotoXY(15, 7);
    WriteLn('This text is at position (15, 7)');

    { Test colors }
    GotoXY(1, 10);
    WriteLn('3. Testing TextColor and TextBackground...');

    GotoXY(1, 12);
    TextColor(1);  { Red }
    WriteLn('Red text');

    TextColor(2);  { Green }
    WriteLn('Green text');

    TextColor(3);  { Yellow }
    WriteLn('Yellow text');

    TextColor(4);  { Blue }
    WriteLn('Blue text');

    TextColor(5);  { Magenta }
    WriteLn('Magenta text');

    TextColor(6);  { Cyan }
    WriteLn('Cyan text');

    { Reset to default }
    TextColor(7);
    TextBackground(0);

    GotoXY(1, 20);
    WriteLn('');
    WriteLn('=== Iteration 2 Complete ===');
end.
