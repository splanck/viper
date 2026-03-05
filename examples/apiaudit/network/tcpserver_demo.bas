' API Audit: Viper.Network.TcpServer (BASIC)
PRINT "=== API Audit: Viper.Network.TcpServer ==="
PRINT "NOTE: TcpServer requires binding to ports. API surface test only."
PRINT "Constructor: Listen(port), ListenAt(address,port)"
PRINT "Properties: Port() -> i64, Address() -> str, IsListening() -> bool"
PRINT "Methods: Accept() -> Tcp, AcceptFor(timeoutMs) -> Tcp"
PRINT "Lifecycle: Close()"
PRINT "=== TcpServer Audit Complete (API surface only) ==="
END
