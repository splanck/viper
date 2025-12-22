{ Test Pascal @ProcedureName syntax for threading support }
{ This test verifies the address-of operator works with procedure names }

program AddressOfTest;

procedure WorkerProc;
begin
    WriteLn('Worker procedure');
end;

procedure AnotherProc;
begin
    WriteLn('Another procedure');
end;

var
    ptr1: Pointer;
    ptr2: Pointer;
begin
    { Get procedure addresses }
    ptr1 := @WorkerProc;
    ptr2 := @AnotherProc;

    { Verify pointers are not nil }
    if ptr1 <> nil then
        WriteLn('ptr1 is valid')
    else
        WriteLn('ptr1 is nil');

    if ptr2 <> nil then
        WriteLn('ptr2 is valid')
    else
        WriteLn('ptr2 is nil');

    { Verify they are different }
    if ptr1 <> ptr2 then
        WriteLn('Pointers are different')
    else
        WriteLn('Pointers are same');

    WriteLn('RESULT: ok');
end.
