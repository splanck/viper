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
Viper.Core.Diagnostics.Assert(html.Length > 0, "http.get")

DIM htmlBytes AS Viper.Collections.Bytes
htmlBytes = Viper.Network.Http.GetBytes(baseUrl)
Viper.Core.Diagnostics.Assert(htmlBytes.Len > 0, "http.getbytes")

DIM postRes AS STRING
postRes = Viper.Network.Http.Post(baseUrl, "name=test")
Viper.Core.Diagnostics.Assert(postRes.Length > 0, "http.post")

DIM payload AS Viper.Collections.Bytes
payload = NEW Viper.Collections.Bytes(3)
payload.Set(0, 97)
payload.Set(1, 98)
payload.Set(2, 99)

DIM postBytes AS Viper.Collections.Bytes
postBytes = Viper.Network.Http.PostBytes(baseUrl, payload)
Viper.Core.Diagnostics.Assert(postBytes.Len > 0, "http.postbytes")

DIM tmpDir AS STRING
tmpDir = Viper.IO.Path.Join(Viper.Machine.Temp, "viper_http")
Viper.IO.Dir.MakeAll(tmpDir)
DIM outPath AS STRING
outPath = Viper.IO.Path.Join(tmpDir, "example.html")
DIM ok AS INTEGER
ok = Viper.Network.Http.Download(baseUrl, outPath)
Viper.Core.Diagnostics.Assert(ok <> 0, "http.download")
Viper.Core.Diagnostics.Assert(Viper.IO.File.Size(outPath) > 0, "http.download.size")
Viper.IO.File.Delete(outPath)
Viper.IO.Dir.Remove(tmpDir)

DIM headRes AS Viper.Network.HttpRes
headRes = Viper.Network.Http.Head(baseUrl)
Viper.Core.Diagnostics.Assert(headRes.Status >= 200, "http.head.status")
Viper.Core.Diagnostics.Assert(headRes.StatusText <> "", "http.head.statustext")
Viper.Core.Diagnostics.Assert(headRes.IsOk(), "http.head.isok")

DIM headHeaders AS Viper.Collections.Map
headHeaders = headRes.Headers
Viper.Core.Diagnostics.Assert(headHeaders.Len > 0, "http.head.headers")

DIM headType AS STRING
headType = headRes.Header("content-type")
Viper.Core.Diagnostics.Assert(headType <> "", "http.head.header")

DIM headBody AS Viper.Collections.Bytes
headBody = headRes.Body()
Viper.Core.Diagnostics.Assert(headBody.Len >= 0, "http.head.body")
DIM headBodyStr AS STRING
headBodyStr = headRes.BodyStr()
Viper.Core.Diagnostics.Assert(headBodyStr.Length >= 0, "http.head.bodystr")

DIM req AS Viper.Network.HttpReq
req = Viper.Network.HttpReq.New("GET", baseUrl)
req.SetHeader("Accept", "text/html")
req.SetBodyStr("hello")
req.SetBody(payload)
req.SetTimeout(5000)

DIM res AS Viper.Network.HttpRes
res = req.Send()
Viper.Core.Diagnostics.Assert(res.Status >= 200, "httpreq.status")
Viper.Core.Diagnostics.Assert(res.StatusText <> "", "httpreq.statustext")
Viper.Core.Diagnostics.Assert(res.IsOk(), "httpreq.isok")

DIM resHeaders AS Viper.Collections.Map
resHeaders = res.Headers
Viper.Core.Diagnostics.Assert(resHeaders.Len > 0, "httpreq.headers")

DIM resBody AS Viper.Collections.Bytes
resBody = res.Body()
Viper.Core.Diagnostics.Assert(resBody.Len > 0, "httpreq.body")

DIM resBodyStr AS STRING
resBodyStr = res.BodyStr()
Viper.Core.Diagnostics.Assert(resBodyStr.Length > 0, "httpreq.bodystr")

DIM resHeader AS STRING
resHeader = res.Header("content-type")
Viper.Core.Diagnostics.Assert(resHeader <> "", "httpreq.header")

DIM url AS Viper.Network.Url
url = Viper.Network.Url.Parse("http://user:pass@example.com:8080/path/to?foo=bar&x=1#frag")
Viper.Core.Diagnostics.AssertEqStr(url.Scheme, "http", "url.scheme")
Viper.Core.Diagnostics.AssertEqStr(url.User, "user", "url.user")
Viper.Core.Diagnostics.AssertEqStr(url.Pass, "pass", "url.pass")
Viper.Core.Diagnostics.AssertEqStr(url.Host, "example.com", "url.host")
Viper.Core.Diagnostics.AssertEq(url.Port, 8080, "url.port")
Viper.Core.Diagnostics.AssertEqStr(url.Path, "/path/to", "url.path")
Viper.Core.Diagnostics.AssertEqStr(url.Query, "foo=bar&x=1", "url.query")
Viper.Core.Diagnostics.AssertEqStr(url.Fragment, "frag", "url.fragment")
Viper.Core.Diagnostics.Assert(url.Authority <> "", "url.authority")
Viper.Core.Diagnostics.Assert(url.HostPort <> "", "url.hostport")
Viper.Core.Diagnostics.Assert(url.Full <> "", "url.full")

DIM clone AS Viper.Network.Url
clone = url.Clone()
Viper.Core.Diagnostics.AssertEqStr(clone.Full, url.Full, "url.clone")

Viper.Core.Diagnostics.Assert(url.HasQueryParam("foo"), "url.hasparam")
Viper.Core.Diagnostics.AssertEqStr(url.GetQueryParam("foo"), "bar", "url.getparam")
url.SetQueryParam("new", "1")
Viper.Core.Diagnostics.Assert(url.HasQueryParam("new"), "url.setparam")
url.DelQueryParam("x")
Viper.Core.Diagnostics.Assert(url.HasQueryParam("x") = 0, "url.delparam")

DIM qmap AS Viper.Collections.Map
qmap = url.QueryMap()
Viper.Core.Diagnostics.Assert(qmap.Len >= 2, "url.querymap")
Viper.Core.Diagnostics.AssertEqStr(Viper.Core.Box.ToStr(qmap.Get("foo")), "bar", "url.querymap.foo")

DIM base AS Viper.Network.Url
base = Viper.Network.Url.Parse("http://example.com/a/b/c")
DIM resolved AS Viper.Network.Url
resolved = base.Resolve("d")
Viper.Core.Diagnostics.AssertEqStr(resolved.Full, "http://example.com/a/b/d", "url.resolve")

DIM built AS Viper.Network.Url
built = Viper.Network.Url.New()
built.Scheme = "http"
built.Host = "example.com"
built.Path = "/docs"
built.SetQueryParam("q", "test")
Viper.Core.Diagnostics.Assert(Viper.String.Has(built.Full, "http://example.com/docs"), "url.new")

DIM enc AS STRING
enc = Viper.Network.Url.Encode("hello world!")
Viper.Core.Diagnostics.AssertEqStr(enc, "hello%20world%21", "url.encode")

DIM dec AS STRING
dec = Viper.Network.Url.Decode(enc)
Viper.Core.Diagnostics.AssertEqStr(dec, "hello world!", "url.decode")

DIM params AS Viper.Collections.Map
params = Viper.Collections.Map.New()
params.Set("name", Viper.Core.Box.Str("John Doe"))
params.Set("city", Viper.Core.Box.Str("New York"))

DIM queryStr AS STRING
queryStr = Viper.Network.Url.EncodeQuery(params)
Viper.Core.Diagnostics.Assert(Viper.String.Has(queryStr, "name=John%20Doe"), "url.encodequery.name")
Viper.Core.Diagnostics.Assert(Viper.String.Has(queryStr, "city=New%20York"), "url.encodequery.city")

DIM parsed AS Viper.Collections.Map
parsed = Viper.Network.Url.DecodeQuery("a=1&b=2")
Viper.Core.Diagnostics.AssertEqStr(Viper.Core.Box.ToStr(parsed.Get("a")), "1", "url.decodequery")

Viper.Core.Diagnostics.Assert(Viper.Network.Url.IsValid("http://example.com"), "url.isvalid")
Viper.Core.Diagnostics.Assert(Viper.Network.Url.IsValid("http://") = FALSE, "url.isvalid.false")

PRINT "RESULT: ok"
END
