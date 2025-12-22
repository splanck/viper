' EXPECT_OUT: RESULT: ok
' COVER: Viper.Network.Http.Get
' COVER: Viper.Network.Http.GetBytes
' COVER: Viper.Network.Http.Post
' COVER: Viper.Network.Http.PostBytes
' COVER: Viper.Network.Http.Download
' COVER: Viper.Network.Http.Head
' COVER: Viper.Network.HttpReq.New
' COVER: Viper.Network.HttpReq.Send
' COVER: Viper.Network.HttpReq.SetBody
' COVER: Viper.Network.HttpReq.SetBodyStr
' COVER: Viper.Network.HttpReq.SetHeader
' COVER: Viper.Network.HttpReq.SetTimeout
' COVER: Viper.Network.HttpRes.Headers
' COVER: Viper.Network.HttpRes.Status
' COVER: Viper.Network.HttpRes.StatusText
' COVER: Viper.Network.HttpRes.Body
' COVER: Viper.Network.HttpRes.BodyStr
' COVER: Viper.Network.HttpRes.Header
' COVER: Viper.Network.HttpRes.IsOk
' COVER: Viper.Network.Url.Authority
' COVER: Viper.Network.Url.Fragment
' COVER: Viper.Network.Url.Full
' COVER: Viper.Network.Url.Host
' COVER: Viper.Network.Url.HostPort
' COVER: Viper.Network.Url.Pass
' COVER: Viper.Network.Url.Path
' COVER: Viper.Network.Url.Port
' COVER: Viper.Network.Url.Query
' COVER: Viper.Network.Url.Scheme
' COVER: Viper.Network.Url.User
' COVER: Viper.Network.Url.Clone
' COVER: Viper.Network.Url.Decode
' COVER: Viper.Network.Url.DecodeQuery
' COVER: Viper.Network.Url.DelQueryParam
' COVER: Viper.Network.Url.Encode
' COVER: Viper.Network.Url.EncodeQuery
' COVER: Viper.Network.Url.GetQueryParam
' COVER: Viper.Network.Url.HasQueryParam
' COVER: Viper.Network.Url.IsValid
' COVER: Viper.Network.Url.New
' COVER: Viper.Network.Url.Parse
' COVER: Viper.Network.Url.QueryMap
' COVER: Viper.Network.Url.Resolve
' COVER: Viper.Network.Url.SetQueryParam

DIM baseUrl AS STRING
baseUrl = "http://example.com"

DIM html AS STRING
html = Viper.Network.Http.Get(baseUrl)
Viper.Diagnostics.Assert(html.Length > 0, "http.get")

DIM htmlBytes AS Viper.Collections.Bytes
htmlBytes = Viper.Network.Http.GetBytes(baseUrl)
Viper.Diagnostics.Assert(htmlBytes.Len > 0, "http.getbytes")

DIM postRes AS STRING
postRes = Viper.Network.Http.Post(baseUrl, "name=test")
Viper.Diagnostics.Assert(postRes.Length > 0, "http.post")

DIM payload AS Viper.Collections.Bytes
payload = NEW Viper.Collections.Bytes(3)
payload.Set(0, 97)
payload.Set(1, 98)
payload.Set(2, 99)

DIM postBytes AS Viper.Collections.Bytes
postBytes = Viper.Network.Http.PostBytes(baseUrl, payload)
Viper.Diagnostics.Assert(postBytes.Len > 0, "http.postbytes")

DIM tmpDir AS STRING
tmpDir = Viper.IO.Path.Join(Viper.Machine.Temp, "viper_http")
Viper.IO.Dir.MakeAll(tmpDir)
DIM outPath AS STRING
outPath = Viper.IO.Path.Join(tmpDir, "example.html")
DIM ok AS INTEGER
ok = Viper.Network.Http.Download(baseUrl, outPath)
Viper.Diagnostics.Assert(ok, "http.download")
Viper.Diagnostics.Assert(Viper.IO.File.Size(outPath) > 0, "http.download.size")
Viper.IO.File.Delete(outPath)
Viper.IO.Dir.Remove(tmpDir)

DIM headRes AS OBJECT
headRes = Viper.Network.Http.Head(baseUrl)
Viper.Diagnostics.Assert(headRes.Status >= 200, "http.head.status")
Viper.Diagnostics.Assert(headRes.StatusText <> "", "http.head.statustext")
Viper.Diagnostics.Assert(headRes.IsOk(), "http.head.isok")

DIM headHeaders AS OBJECT
headHeaders = headRes.Headers
Viper.Diagnostics.Assert(headHeaders.Len > 0, "http.head.headers")

DIM headType AS STRING
headType = headRes.Header("content-type")
Viper.Diagnostics.Assert(headType <> "", "http.head.header")

DIM headBody AS Viper.Collections.Bytes
headBody = headRes.Body()
Viper.Diagnostics.Assert(headBody.Len >= 0, "http.head.body")
DIM headBodyStr AS STRING
headBodyStr = headRes.BodyStr()
Viper.Diagnostics.Assert(headBodyStr.Length >= 0, "http.head.bodystr")

DIM req AS OBJECT
req = Viper.Network.HttpReq.New("GET", baseUrl)
req.SetHeader("Accept", "text/html")
req.SetBodyStr("hello")
req.SetBody(payload)
req.SetTimeout(5000)

DIM res AS OBJECT
res = req.Send()
Viper.Diagnostics.Assert(res.Status >= 200, "httpreq.status")
Viper.Diagnostics.Assert(res.StatusText <> "", "httpreq.statustext")
Viper.Diagnostics.Assert(res.IsOk(), "httpreq.isok")

DIM resHeaders AS OBJECT
resHeaders = res.Headers
Viper.Diagnostics.Assert(resHeaders.Len > 0, "httpreq.headers")

DIM resBody AS Viper.Collections.Bytes
resBody = res.Body()
Viper.Diagnostics.Assert(resBody.Len > 0, "httpreq.body")

DIM resBodyStr AS STRING
resBodyStr = res.BodyStr()
Viper.Diagnostics.Assert(resBodyStr.Length > 0, "httpreq.bodystr")

DIM resHeader AS STRING
resHeader = res.Header("content-type")
Viper.Diagnostics.Assert(resHeader <> "", "httpreq.header")

DIM url AS OBJECT
url = Viper.Network.Url.Parse("http://user:pass@example.com:8080/path/to?foo=bar&x=1#frag")
Viper.Diagnostics.AssertEqStr(url.Scheme, "http", "url.scheme")
Viper.Diagnostics.AssertEqStr(url.User, "user", "url.user")
Viper.Diagnostics.AssertEqStr(url.Pass, "pass", "url.pass")
Viper.Diagnostics.AssertEqStr(url.Host, "example.com", "url.host")
Viper.Diagnostics.AssertEq(url.Port, 8080, "url.port")
Viper.Diagnostics.AssertEqStr(url.Path, "/path/to", "url.path")
Viper.Diagnostics.AssertEqStr(url.Query, "foo=bar&x=1", "url.query")
Viper.Diagnostics.AssertEqStr(url.Fragment, "frag", "url.fragment")
Viper.Diagnostics.Assert(url.Authority <> "", "url.authority")
Viper.Diagnostics.Assert(url.HostPort <> "", "url.hostport")
Viper.Diagnostics.Assert(url.Full <> "", "url.full")

DIM clone AS OBJECT
clone = url.Clone()
Viper.Diagnostics.AssertEqStr(clone.Full, url.Full, "url.clone")

Viper.Diagnostics.Assert(url.HasQueryParam("foo"), "url.hasparam")
Viper.Diagnostics.AssertEqStr(url.GetQueryParam("foo"), "bar", "url.getparam")
url.SetQueryParam("new", "1")
Viper.Diagnostics.Assert(url.HasQueryParam("new"), "url.setparam")
url.DelQueryParam("x")
Viper.Diagnostics.Assert(url.HasQueryParam("x") = 0, "url.delparam")

DIM qmap AS OBJECT
qmap = url.QueryMap()
Viper.Diagnostics.Assert(qmap.Len >= 2, "url.querymap")
Viper.Diagnostics.AssertEqStr(qmap.Get("foo"), "bar", "url.querymap.foo")

DIM base AS OBJECT
base = Viper.Network.Url.Parse("http://example.com/a/b/c")
DIM resolved AS OBJECT
resolved = base.Resolve("d")
Viper.Diagnostics.AssertEqStr(resolved.Full, "http://example.com/a/b/d", "url.resolve")

DIM built AS OBJECT
built = Viper.Network.Url.New()
built.Scheme = "http"
built.Host = "example.com"
built.Path = "/docs"
built.SetQueryParam("q", "test")
Viper.Diagnostics.Assert(Viper.String.Has(built.Full, "http://example.com/docs"), "url.new")

DIM enc AS STRING
enc = Viper.Network.Url.Encode("hello world!")
Viper.Diagnostics.AssertEqStr(enc, "hello%20world%21", "url.encode")

DIM dec AS STRING
dec = Viper.Network.Url.Decode(enc)
Viper.Diagnostics.AssertEqStr(dec, "hello world!", "url.decode")

DIM params AS Viper.Collections.Map
params = Viper.Collections.Map.New()
params.Set("name", "John Doe")
params.Set("city", "New York")

DIM queryStr AS STRING
queryStr = Viper.Network.Url.EncodeQuery(params)
Viper.Diagnostics.Assert(Viper.String.Has(queryStr, "name=John%20Doe"), "url.encodequery.name")
Viper.Diagnostics.Assert(Viper.String.Has(queryStr, "city=New%20York"), "url.encodequery.city")

DIM parsed AS OBJECT
parsed = Viper.Network.Url.DecodeQuery("a=1&b=2")
Viper.Diagnostics.AssertEqStr(parsed.Get("a"), "1", "url.decodequery")

Viper.Diagnostics.Assert(Viper.Network.Url.IsValid("http://example.com"), "url.isvalid")
Viper.Diagnostics.Assert(Viper.Network.Url.IsValid("http://") = 0, "url.isvalid.false")

PRINT "RESULT: ok"
END
