{ Viper Pascal exception handling test }
program ExceptionHandling;
var
  e: Exception;
begin
  { Try-except with handler }
  try
    WriteLn('In try block')
  except
    on E: Exception do
      WriteLn('Caught exception')
  end;

  { Try-finally }
  try
    WriteLn('Try with finally')
  finally
    WriteLn('Finally runs')
  end
end.
