' pattern_demo.bas
PRINT "=== Viper.Text.Pattern Demo ==="
PRINT Viper.Text.Pattern.IsMatch("hello123", "[a-z]+[0-9]+")
PRINT Viper.Text.Pattern.Find("abc123def", "[0-9]+")
PRINT Viper.Text.Pattern.FindPos("abc123def", "[0-9]+")
PRINT Viper.Text.Pattern.Replace("hello world", "[aeiou]", "*")
PRINT Viper.Text.Pattern.ReplaceFirst("hello world", "[aeiou]", "*")
PRINT Viper.Text.Pattern.Escape("a.b+c")
PRINT "done"
END
