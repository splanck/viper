' EXPECT_OUT: RESULT: ok
' COVER: Viper.Network.Dns.IsIP
' COVER: Viper.Network.Dns.IsIPv4
' COVER: Viper.Network.Dns.IsIPv6
' COVER: Viper.Network.Dns.LocalAddrs
' COVER: Viper.Network.Dns.LocalHost
' COVER: Viper.Network.Dns.Resolve
' COVER: Viper.Network.Dns.Resolve4
' COVER: Viper.Network.Dns.Resolve6
' COVER: Viper.Network.Dns.ResolveAll
' COVER: Viper.Network.Dns.Reverse

DIM ip4 AS STRING
ip4 = "127.0.0.1"
DIM ip6 AS STRING
ip6 = "::1"

Viper.Core.Diagnostics.Assert(Viper.Network.Dns.IsIP(ip4), "dns.isip")
Viper.Core.Diagnostics.Assert(Viper.Network.Dns.IsIPv4(ip4), "dns.isipv4")
Viper.Core.Diagnostics.Assert(Viper.Network.Dns.IsIPv6(ip4) = FALSE, "dns.isipv6.false")
Viper.Core.Diagnostics.Assert(Viper.Network.Dns.IsIPv6(ip6), "dns.isipv6")

DIM host AS STRING
host = Viper.Network.Dns.LocalHost()
Viper.Core.Diagnostics.Assert(host <> "", "dns.localhost")

DIM addrs AS Viper.Collections.Seq
addrs = Viper.Network.Dns.LocalAddrs()
Viper.Core.Diagnostics.Assert(addrs.Len >= 1, "dns.localaddrs")

DIM resolved AS STRING
resolved = Viper.Network.Dns.Resolve("localhost")
Viper.Core.Diagnostics.Assert(Viper.Network.Dns.IsIPv4(resolved), "dns.resolve")

DIM resolved4 AS STRING
resolved4 = Viper.Network.Dns.Resolve4(ip4)
Viper.Core.Diagnostics.Assert(Viper.Network.Dns.IsIPv4(resolved4), "dns.resolve4")

DIM resolved6 AS STRING
resolved6 = Viper.Network.Dns.Resolve6(ip6)
Viper.Core.Diagnostics.Assert(Viper.Network.Dns.IsIPv6(resolved6), "dns.resolve6")

DIM all AS Viper.Collections.Seq
all = Viper.Network.Dns.ResolveAll("localhost")
Viper.Core.Diagnostics.Assert(all.Len >= 1, "dns.resolveall")

DIM rev AS STRING
rev = Viper.Network.Dns.Reverse(ip4)
Viper.Core.Diagnostics.Assert(rev <> "", "dns.reverse")

PRINT "RESULT: ok"
END
