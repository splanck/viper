' html_demo.bas
PRINT "=== Viper.Text.Html Demo ==="
PRINT Viper.Text.Html.Escape("<b>Hello</b>")
PRINT Viper.Text.Html.Unescape("&lt;b&gt;Hello&lt;/b&gt;")
PRINT Viper.Text.Html.StripTags("<p>Hello <b>World</b></p>")
PRINT Viper.Text.Html.ToText("<p>Hello</p><p>World</p>")
PRINT "done"
END
