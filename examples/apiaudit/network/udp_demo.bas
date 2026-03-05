' API Audit: Viper.Network.Udp (BASIC)
PRINT "=== API Audit: Viper.Network.Udp ==="
PRINT "NOTE: UDP requires socket binding. API surface test only."
PRINT "Constructor: New(), Bind(port), BindAt(address,port)"
PRINT "Properties: Port() -> i64, Address() -> str, IsBound() -> bool"
PRINT "Send: SendTo(host,port,bytes) -> i64, SendToStr(host,port,str) -> i64"
PRINT "Recv: Recv(maxBytes) -> bytes, RecvFrom(maxBytes) -> bytes, RecvFor(maxBytes,timeoutMs) -> bytes"
PRINT "Sender info: SenderHost() -> str, SenderPort() -> i64"
PRINT "Config: SetBroadcast(enabled), JoinGroup(group), LeaveGroup(group), SetRecvTimeout(ms)"
PRINT "Lifecycle: Close()"
PRINT "=== Udp Audit Complete (API surface only) ==="
END
