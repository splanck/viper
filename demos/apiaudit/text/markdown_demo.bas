' markdown_demo.bas
PRINT "=== Viper.Text.Markdown Demo ==="
PRINT Viper.Text.Markdown.ToHtml("# Hello" + CHR$(10) + "**bold**")
PRINT Viper.Text.Markdown.ToText("# Hello" + CHR$(10) + "**bold** text")
PRINT "done"
END
