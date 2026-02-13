' API Audit: Viper.Network.Tcp (BASIC)
PRINT "=== API Audit: Viper.Network.Tcp ==="
PRINT "NOTE: TCP requires active connections. API surface test only."
PRINT "Constructor: Connect(host,port), ConnectFor(host,port,timeoutMs)"
PRINT "Properties: Host() -> str, Port() -> i64, LocalPort() -> i64, IsOpen() -> bool, Available() -> i64"
PRINT "Send methods: Send(bytes) -> i64, SendStr(str) -> i64, SendAll(bytes)"
PRINT "Recv methods: Recv(maxBytes) -> bytes, RecvStr(maxBytes) -> str, RecvExact(n) -> bytes, RecvLine() -> str"
PRINT "Config: SetRecvTimeout(ms), SetSendTimeout(ms)"
PRINT "Lifecycle: Close()"
PRINT "=== Tcp Audit Complete (API surface only) ==="
END
