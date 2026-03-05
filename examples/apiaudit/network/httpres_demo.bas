' API Audit: Viper.Network.HttpRes (BASIC)
PRINT "=== API Audit: Viper.Network.HttpRes ==="
PRINT "NOTE: HttpRes is returned from HttpReq.Send or Http.Get. API surface test only."
PRINT "Methods: Status() -> i64, StatusText() -> str, Headers() -> str, Body() -> bytes, BodyStr() -> str, Header(key) -> str, IsOk() -> bool"
PRINT "=== HttpRes Audit Complete (API surface only) ==="
END
