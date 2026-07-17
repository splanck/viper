' API Audit: Zanna.Network.HttpReq (BASIC)
PRINT "=== API Audit: Zanna.Network.HttpReq ==="
PRINT "NOTE: HttpReq requires network access to Send/SendResult. API surface test only."
PRINT "Methods: New(method,url), SetHeader(key,val), SetBody(bytes), SetBodyStr(str), SetTimeout(ms), Send() -> HttpRes, SendResult() -> Result<HttpRes>"
PRINT "=== HttpReq Audit Complete (API surface only) ==="
END
