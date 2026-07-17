' EXPECT_OUT: RESULT: ok
' EXPECT_ARGS: alpha beta
' COVER: Zanna.System.Environment.GetArgumentCount
' COVER: Zanna.System.Environment.GetArgument
' COVER: Zanna.System.Environment.GetCommandLine
' COVER: Zanna.System.Environment.GetVariable
' COVER: Zanna.System.Environment.HasVariable
' COVER: Zanna.System.Environment.SetVariable
' COVER: Zanna.System.Environment.IsNative
' COVER: Zanna.System.Exec.Capture
' COVER: Zanna.System.Exec.CaptureArgs
' COVER: Zanna.System.Exec.Run
' COVER: Zanna.System.Exec.RunArgs
' COVER: Zanna.System.Exec.Shell
' COVER: Zanna.System.Exec.ShellCapture
' COVER: Zanna.System.Machine.Cores
' COVER: Zanna.System.Machine.Endian
' COVER: Zanna.System.Machine.Home
' COVER: Zanna.System.Machine.Host
' COVER: Zanna.System.Machine.MemoryFree
' COVER: Zanna.System.Machine.MemoryTotal
' COVER: Zanna.System.Machine.Os
' COVER: Zanna.System.Machine.OsVersion
' COVER: Zanna.System.Machine.TempDir
' COVER: Zanna.System.Machine.User
' COVER: Zanna.Diagnostics.Log.LevelDebug
' COVER: Zanna.Diagnostics.Log.LevelInfo
' COVER: Zanna.Diagnostics.Log.LevelWarn
' COVER: Zanna.Diagnostics.Log.LevelError
' COVER: Zanna.Diagnostics.Log.LevelOff
' COVER: Zanna.Diagnostics.Log.Level
' COVER: Zanna.Diagnostics.Log.Debug
' COVER: Zanna.Diagnostics.Log.Info
' COVER: Zanna.Diagnostics.Log.Warn
' COVER: Zanna.Diagnostics.Log.Error
' COVER: Zanna.Diagnostics.Log.Enabled

DIM argc AS INTEGER
argc = Zanna.System.Environment.GetArgumentCount()
Zanna.Core.Diagnostics.Assert(argc >= 1, "env.argc")
IF argc >= 3 THEN
    Zanna.Core.Diagnostics.AssertEqStr(Zanna.System.Environment.GetArgument(1), "alpha", "env.arg1")
    Zanna.Core.Diagnostics.AssertEqStr(Zanna.System.Environment.GetArgument(2), "beta", "env.arg2")
END IF
Zanna.Core.Diagnostics.Assert(LEN(Zanna.System.Environment.GetCommandLine()) > 0, "env.cmd")

DIM name AS STRING
name = "ZANNA_SWEEP_ENV"
Zanna.System.Environment.SetVariable(name, "ok")
Zanna.Core.Diagnostics.Assert(Zanna.System.Environment.HasVariable(name), "env.has")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.System.Environment.GetVariable(name), "ok", "env.get")
Zanna.Core.Diagnostics.Assert(Zanna.System.Environment.IsNative() = FALSE, "env.native")

DIM args AS Zanna.Collections.Seq
args = Zanna.Collections.Seq.New()
args.Push("hello")
DIM out AS STRING
out = Zanna.System.Exec.CaptureArgs("/bin/echo", args)
Zanna.Core.Diagnostics.Assert(Zanna.String.Trim(out) = "hello", "exec.captureargs")
Zanna.Core.Diagnostics.AssertEq(Zanna.System.Exec.Run("/bin/echo"), 0, "exec.run")
Zanna.Core.Diagnostics.AssertEq(Zanna.System.Exec.RunArgs("/bin/echo", args), 0, "exec.runargs")
Zanna.Core.Diagnostics.AssertEq(Zanna.System.Exec.Shell("echo shell"), 0, "exec.shell")
out = Zanna.System.Exec.ShellCapture("echo shellcap")
Zanna.Core.Diagnostics.Assert(Zanna.String.Trim(out) = "shellcap", "exec.shellcap")
out = Zanna.System.Exec.Capture("/bin/echo")
Zanna.Core.Diagnostics.Assert(Zanna.String.Trim(out) = "", "exec.capture")

Zanna.Core.Diagnostics.Assert(LEN(Zanna.System.Machine.Os) > 0, "machine.os")
Zanna.Core.Diagnostics.Assert(LEN(Zanna.System.Machine.OsVersion) > 0, "machine.osver")
Zanna.Core.Diagnostics.Assert(LEN(Zanna.System.Machine.Host) > 0, "machine.host")
Zanna.Core.Diagnostics.Assert(LEN(Zanna.System.Machine.User) > 0, "machine.user")
Zanna.Core.Diagnostics.Assert(LEN(Zanna.System.Machine.Home) > 0, "machine.home")
Zanna.Core.Diagnostics.Assert(LEN(Zanna.System.Machine.TempDir) > 0, "machine.temp")
Zanna.Core.Diagnostics.Assert(Zanna.System.Machine.Cores > 0, "machine.cores")
Zanna.Core.Diagnostics.Assert(Zanna.System.Machine.MemoryTotal > 0, "machine.memtotal")
Zanna.Core.Diagnostics.Assert(Zanna.System.Machine.MemoryFree >= 0, "machine.memfree")
Zanna.Core.Diagnostics.Assert(LEN(Zanna.System.Machine.Endian) > 0, "machine.endian")

Zanna.Diagnostics.Log.Level = Zanna.Diagnostics.Log.LevelInfo
Zanna.Diagnostics.Log.Debug("debug")
Zanna.Diagnostics.Log.Info("info")
Zanna.Diagnostics.Log.Warn("warn")
Zanna.Diagnostics.Log.Error("error")
Zanna.Core.Diagnostics.Assert(Zanna.Diagnostics.Log.Enabled(Zanna.Diagnostics.Log.LevelInfo), "log.enabled")

PRINT "RESULT: ok"
END
