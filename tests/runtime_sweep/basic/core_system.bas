' EXPECT_OUT: RESULT: ok
' EXPECT_ARGS: alpha beta
' COVER: Viper.System.Environment.GetArgumentCount
' COVER: Viper.System.Environment.GetArgument
' COVER: Viper.System.Environment.GetCommandLine
' COVER: Viper.System.Environment.GetVariable
' COVER: Viper.System.Environment.HasVariable
' COVER: Viper.System.Environment.SetVariable
' COVER: Viper.System.Environment.IsNative
' COVER: Viper.System.Exec.Capture
' COVER: Viper.System.Exec.CaptureArgs
' COVER: Viper.System.Exec.Run
' COVER: Viper.System.Exec.RunArgs
' COVER: Viper.System.Exec.Shell
' COVER: Viper.System.Exec.ShellCapture
' COVER: Viper.System.Machine.Cores
' COVER: Viper.System.Machine.Endian
' COVER: Viper.System.Machine.Home
' COVER: Viper.System.Machine.Host
' COVER: Viper.System.Machine.MemFree
' COVER: Viper.System.Machine.MemTotal
' COVER: Viper.System.Machine.OS
' COVER: Viper.System.Machine.OSVer
' COVER: Viper.System.Machine.Temp
' COVER: Viper.System.Machine.User
' COVER: Viper.Diagnostics.Log.DEBUG
' COVER: Viper.Diagnostics.Log.INFO
' COVER: Viper.Diagnostics.Log.WARN
' COVER: Viper.Diagnostics.Log.ERROR
' COVER: Viper.Diagnostics.Log.OFF
' COVER: Viper.Diagnostics.Log.Level
' COVER: Viper.Diagnostics.Log.Debug
' COVER: Viper.Diagnostics.Log.Info
' COVER: Viper.Diagnostics.Log.Warn
' COVER: Viper.Diagnostics.Log.Error
' COVER: Viper.Diagnostics.Log.Enabled

DIM argc AS INTEGER
argc = Viper.System.Environment.GetArgumentCount()
Viper.Core.Diagnostics.Assert(argc >= 1, "env.argc")
IF argc >= 3 THEN
    Viper.Core.Diagnostics.AssertEqStr(Viper.System.Environment.GetArgument(1), "alpha", "env.arg1")
    Viper.Core.Diagnostics.AssertEqStr(Viper.System.Environment.GetArgument(2), "beta", "env.arg2")
END IF
Viper.Core.Diagnostics.Assert(Viper.System.Environment.GetCommandLine() <> "", "env.cmd")

DIM name AS STRING
name = "VIPER_SWEEP_ENV"
Viper.System.Environment.SetVariable(name, "ok")
Viper.Core.Diagnostics.Assert(Viper.System.Environment.HasVariable(name), "env.has")
Viper.Core.Diagnostics.AssertEqStr(Viper.System.Environment.GetVariable(name), "ok", "env.get")
Viper.Core.Diagnostics.Assert(Viper.System.Environment.IsNative() = FALSE, "env.native")

DIM args AS Viper.Collections.Seq
args = Viper.Collections.Seq.New()
args.Push("hello")
DIM out AS STRING
out = Viper.System.Exec.CaptureArgs("/bin/echo", args)
Viper.Core.Diagnostics.Assert(Viper.String.Trim(out) = "hello", "exec.captureargs")
Viper.Core.Diagnostics.AssertEq(Viper.System.Exec.Run("/bin/echo"), 0, "exec.run")
Viper.Core.Diagnostics.AssertEq(Viper.System.Exec.RunArgs("/bin/echo", args), 0, "exec.runargs")
Viper.Core.Diagnostics.AssertEq(Viper.System.Exec.Shell("echo shell"), 0, "exec.shell")
out = Viper.System.Exec.ShellCapture("echo shellcap")
Viper.Core.Diagnostics.Assert(Viper.String.Trim(out) = "shellcap", "exec.shellcap")
out = Viper.System.Exec.Capture("/bin/echo")
Viper.Core.Diagnostics.Assert(Viper.String.Trim(out) = "", "exec.capture")

Viper.Core.Diagnostics.Assert(Viper.System.Machine.OS <> "", "machine.os")
Viper.Core.Diagnostics.Assert(Viper.System.Machine.OSVer <> "", "machine.osver")
Viper.Core.Diagnostics.Assert(Viper.System.Machine.Host <> "", "machine.host")
Viper.Core.Diagnostics.Assert(Viper.System.Machine.User <> "", "machine.user")
Viper.Core.Diagnostics.Assert(Viper.System.Machine.Home <> "", "machine.home")
Viper.Core.Diagnostics.Assert(Viper.System.Machine.Temp <> "", "machine.temp")
Viper.Core.Diagnostics.Assert(Viper.System.Machine.Cores > 0, "machine.cores")
Viper.Core.Diagnostics.Assert(Viper.System.Machine.MemTotal > 0, "machine.memtotal")
Viper.Core.Diagnostics.Assert(Viper.System.Machine.MemFree >= 0, "machine.memfree")
Viper.Core.Diagnostics.Assert(Viper.System.Machine.Endian <> "", "machine.endian")

Viper.Diagnostics.Log.Level = Viper.Diagnostics.Log.INFO
Viper.Diagnostics.Log.Debug("debug")
Viper.Diagnostics.Log.Info("info")
Viper.Diagnostics.Log.Warn("warn")
Viper.Diagnostics.Log.Error("error")
Viper.Core.Diagnostics.Assert(Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.INFO), "log.enabled")

PRINT "RESULT: ok"
END
