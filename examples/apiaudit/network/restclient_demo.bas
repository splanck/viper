' API Audit: Viper.Network.RestClient (BASIC)
PRINT "=== API Audit: Viper.Network.RestClient ==="

PRINT "--- New ---"
DIM rc AS OBJECT = Viper.Network.RestClient.New("https://api.example.com")
PRINT rc.BaseUrl

PRINT "--- SetHeader ---"
rc.SetHeader("Content-Type", "application/json")

PRINT "--- SetAuthBearer ---"
rc.SetAuthBearer("test-token-123")

PRINT "--- ClearAuth ---"
rc.ClearAuth()

PRINT "--- SetTimeout ---"
rc.SetTimeout(5000)

PRINT "NOTE: HTTP methods (Get, Post, Put, etc.) require live server."
PRINT "Full method list: Get(path), Post(path,body), Put(path,body), Patch(path,body), Delete(path), Head(path)"
PRINT "JSON methods: GetJson(path), PostJson(path,obj), PutJson(path,obj), PatchJson(path,obj), DeleteJson(path)"
PRINT "Response props: LastStatus(), LastResponse(), LastOk()"
PRINT "Auth methods: SetAuthBearer(token), SetAuthBasic(user,pass), ClearAuth()"
PRINT "Config: BaseUrl prop, SetHeader(k,v), DelHeader(k), SetTimeout(ms)"

PRINT "=== RestClient Audit Complete ==="
END
