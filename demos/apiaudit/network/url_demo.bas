' url_demo.bas
PRINT "=== Viper.Network.Url Demo ==="
PRINT Viper.Network.Url.IsValid("https://example.com/path?q=1")
PRINT Viper.Network.Url.IsValid("not a url")
PRINT Viper.Network.Url.Encode("hello world")
PRINT Viper.Network.Url.Decode("hello%20world")
PRINT "done"
END
