' API Audit: Viper.Network.Dns (BASIC)
PRINT "=== API Audit: Viper.Network.Dns ==="

PRINT "--- IsIPv4 ---"
PRINT Viper.Network.Dns.IsIPv4("192.168.1.1")
PRINT Viper.Network.Dns.IsIPv4("not-an-ip")
PRINT Viper.Network.Dns.IsIPv4("::1")

PRINT "--- IsIPv6 ---"
PRINT Viper.Network.Dns.IsIPv6("::1")
PRINT Viper.Network.Dns.IsIPv6("fe80::1")
PRINT Viper.Network.Dns.IsIPv6("192.168.1.1")

PRINT "--- IsIP ---"
PRINT Viper.Network.Dns.IsIP("192.168.1.1")
PRINT Viper.Network.Dns.IsIP("::1")
PRINT Viper.Network.Dns.IsIP("not-an-ip")

PRINT "--- LocalHost ---"
PRINT Viper.Network.Dns.LocalHost()

PRINT "--- Resolve (localhost) ---"
PRINT Viper.Network.Dns.Resolve("localhost")

PRINT "=== Dns Audit Complete ==="
END
