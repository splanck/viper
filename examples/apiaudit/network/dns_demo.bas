' API Audit: Zanna.Network.Dns (BASIC)
PRINT "=== API Audit: Zanna.Network.Dns ==="

PRINT "--- IsIPv4 ---"
PRINT Zanna.Network.Dns.IsIpv4("192.168.1.1")
PRINT Zanna.Network.Dns.IsIpv4("not-an-ip")
PRINT Zanna.Network.Dns.IsIpv4("::1")

PRINT "--- IsIPv6 ---"
PRINT Zanna.Network.Dns.IsIpv6("::1")
PRINT Zanna.Network.Dns.IsIpv6("fe80::1")
PRINT Zanna.Network.Dns.IsIpv6("192.168.1.1")

PRINT "--- IsIP ---"
PRINT Zanna.Network.Dns.IsIP("192.168.1.1")
PRINT Zanna.Network.Dns.IsIP("::1")
PRINT Zanna.Network.Dns.IsIP("not-an-ip")

PRINT "--- LocalHost ---"
PRINT Zanna.Network.Dns.LocalHost()

PRINT "--- Resolve (localhost) ---"
PRINT Zanna.Network.Dns.Resolve("localhost")

PRINT "=== Dns Audit Complete ==="
END
