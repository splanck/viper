{ Pacman Pascal - ReadKey Test }
program TestReadKey;

uses Crt;

var
    key: String;
    count: Integer;

begin
    WriteLn('=== ReadKey Test ===');
    WriteLn('Testing keyboard input with piped input...');
    WriteLn('');

    count := 0;
    while count < 5 do
    begin
        key := ReadKey;
        WriteLn('Got key: ''', key, '''');

        if (key = 'q') or (key = 'Q') then
        begin
            WriteLn('Quit key detected!');
            count := 999;
        end;

        count := count + 1;
    end;

    WriteLn('');
    WriteLn('Test complete after ', count, ' iterations');
end.
