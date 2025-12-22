' EXPECT_OUT: RESULT: ok
' EXPECT_ARGS: alpha beta
' COVER: Viper.Environment.GetArgumentCount
' COVER: Viper.Environment.GetArgument
' COVER: Viper.Environment.GetCommandLine
' COVER: Viper.Environment.GetVariable
' COVER: Viper.Environment.HasVariable
' COVER: Viper.Environment.SetVariable
' COVER: Viper.Environment.IsNative
' COVER: Viper.Exec.Capture
' COVER: Viper.Exec.CaptureArgs
' COVER: Viper.Exec.Run
' COVER: Viper.Exec.RunArgs
' COVER: Viper.Exec.Shell
' COVER: Viper.Exec.ShellCapture
' COVER: Viper.Machine.Cores
' COVER: Viper.Machine.Endian
' COVER: Viper.Machine.Home
' COVER: Viper.Machine.Host
' COVER: Viper.Machine.MemFree
' COVER: Viper.Machine.MemTotal
' COVER: Viper.Machine.OS
' COVER: Viper.Machine.OSVer
' COVER: Viper.Machine.Temp
' COVER: Viper.Machine.User
' COVER: Viper.Log.DEBUG
' COVER: Viper.Log.INFO
' COVER: Viper.Log.WARN
' COVER: Viper.Log.ERROR
' COVER: Viper.Log.OFF
' COVER: Viper.Log.Level
' COVER: Viper.Log.Debug
' COVER: Viper.Log.Info
' COVER: Viper.Log.Warn
' COVER: Viper.Log.Error
' COVER: Viper.Log.Enabled

DIM argc AS INTEGER
argc = Viper.Environment.GetArgumentCount()
Viper.Diagnostics.Assert(argc >= 1, "env.argc")
IF argc >= 3 THEN
    Viper.Diagnostics.AssertEqStr(Viper.Environment.GetArgument(1), "alpha", "env.arg1")
    Viper.Diagnostics.AssertEqStr(Viper.Environment.GetArgument(2), "beta", "env.arg2")
END IF
Viper.Diagnostics.Assert(Viper.Environment.GetCommandLine() <> "", "env.cmd")

DIM name AS STRING
name = "VIPER_SWEEP_ENV"
Viper.Environment.SetVariable(name, "ok")
Viper.Diagnostics.Assert(Viper.Environment.HasVariable(name), "env.has")
Viper.Diagnostics.AssertEqStr(Viper.Environment.GetVariable(name), "ok", "env.get")
Viper.Diagnostics.Assert(Viper.Environment.IsNative() = FALSE, "env.native")

DIM args AS Viper.Collections.Seq
args = Viper.Collections.Seq.New()
args.Push("hello")
DIM out AS STRING
out = Viper.Exec.CaptureArgs("/bin/echo", args)
Viper.Diagnostics.Assert(Viper.String.Trim(out) = "hello", "exec.captureargs")
Viper.Diagnostics.AssertEq(Viper.Exec.Run("/bin/echo"), 0, "exec.run")
Viper.Diagnostics.AssertEq(Viper.Exec.RunArgs("/bin/echo", args), 0, "exec.runargs")
Viper.Diagnostics.AssertEq(Viper.Exec.Shell("echo shell"), 0, "exec.shell")
out = Viper.Exec.ShellCapture("echo shellcap")
Viper.Diagnostics.Assert(Viper.String.Trim(out) = "shellcap", "exec.shellcap")
out = Viper.Exec.Capture("/bin/echo")
Viper.Diagnostics.Assert(Viper.String.Trim(out) = "", "exec.capture")

Viper.Diagnostics.Assert(Viper.Machine.OS <> "", "machine.os")
Viper.Diagnostics.Assert(Viper.Machine.OSVer <> "", "machine.osver")
Viper.Diagnostics.Assert(Viper.Machine.Host <> "", "machine.host")
Viper.Diagnostics.Assert(Viper.Machine.User <> "", "machine.user")
Viper.Diagnostics.Assert(Viper.Machine.Home <> "", "machine.home")
Viper.Diagnostics.Assert(Viper.Machine.Temp <> "", "machine.temp")
Viper.Diagnostics.Assert(Viper.Machine.Cores > 0, "machine.cores")
Viper.Diagnostics.Assert(Viper.Machine.MemTotal > 0, "machine.memtotal")
Viper.Diagnostics.Assert(Viper.Machine.MemFree >= 0, "machine.memfree")
Viper.Diagnostics.Assert(Viper.Machine.Endian <> "", "machine.endian")

Viper.Log.Level = Viper.Log.INFO
Viper.Log.Debug("debug")
Viper.Log.Info("info")
Viper.Log.Warn("warn")
Viper.Log.Error("error")
Viper.Diagnostics.Assert(Viper.Log.Enabled(Viper.Log.INFO), "log.enabled")

PRINT "RESULT: ok"
END
