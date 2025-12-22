' Viper.Text Demo - Text Processing Utilities
' This demo showcases text formatting, encoding, and pattern matching

' === StringBuilder ===
PRINT "=== StringBuilder ==="
DIM sb AS Viper.Text.StringBuilder
sb = Viper.Text.StringBuilder.New()
sb.Append("Hello")
sb.Append(" ")
sb.Append("World")
sb.AppendLine("!")
sb.Append("Done.")
PRINT "Result: "; sb.ToString()
PRINT "Length: "; sb.Length
sb.Clear()
PRINT "After Clear, Length: "; sb.Length
PRINT

' === Guid ===
PRINT "=== Guid ==="
PRINT "New GUID: "; Viper.Text.Guid.New()
PRINT "New GUID: "; Viper.Text.Guid.New()
PRINT

' === Codec (Base64/Hex) ===
PRINT "=== Codec ==="
DIM encoded AS STRING
DIM decoded AS STRING
encoded = Viper.Text.Codec.Base64Enc("Hello, World!")
PRINT "Base64 Encode 'Hello, World!': "; encoded
decoded = Viper.Text.Codec.Base64Dec(encoded)
PRINT "Base64 Decode: "; decoded
encoded = Viper.Text.Codec.HexEnc("Hi!")
PRINT "Hex Encode 'Hi!': "; encoded
decoded = Viper.Text.Codec.HexDec(encoded)
PRINT "Hex Decode: "; decoded
PRINT

' === Pattern (Regex - Static API) ===
PRINT "=== Pattern ==="
PRINT "Pattern: [0-9]+"
PRINT "IsMatch('[0-9]+', 'abc123'): "; Viper.Text.Pattern.IsMatch("[0-9]+", "abc123")
PRINT "IsMatch('[0-9]+', 'hello'): "; Viper.Text.Pattern.IsMatch("[0-9]+", "hello")
PRINT

' === Template (Static API) ===
PRINT "=== Template ==="
DIM data AS Viper.Collections.Map
data = Viper.Collections.Map.New()
data.Set("name", "Alice")
data.Set("age", "30")
DIM tpl AS STRING
tpl = "Hello, {{name}}! You are {{age}} years old."
PRINT "Template: "; tpl
PRINT "Rendered: "; Viper.Text.Template.Render(tpl, data)
PRINT "Has 'name': "; Viper.Text.Template.Has(tpl, "name")
PRINT "Has 'email': "; Viper.Text.Template.Has(tpl, "email")

END
