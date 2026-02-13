' codec_demo.bas
PRINT "=== Viper.Text.Codec Demo ==="
PRINT Viper.Text.Codec.Base64Enc("Hello")
PRINT Viper.Text.Codec.Base64Dec("SGVsbG8=")
PRINT Viper.Text.Codec.HexEnc("Hi")
PRINT Viper.Text.Codec.HexDec("4869")
PRINT Viper.Text.Codec.UrlEncode("hello world")
PRINT Viper.Text.Codec.UrlDecode("hello%20world")
PRINT "done"
END
