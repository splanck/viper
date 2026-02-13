' string_demo.bas — Viper.String
PRINT "=== Viper.String Demo ==="

PRINT "--- Case ---"
PRINT Viper.String.ToUpper("hello")
PRINT Viper.String.ToLower("HELLO")
PRINT Viper.String.Capitalize("hello world")
PRINT Viper.String.Title("hello world")
PRINT Viper.String.CamelCase("hello world")
PRINT Viper.String.PascalCase("hello world")
PRINT Viper.String.SnakeCase("helloWorld")
PRINT Viper.String.KebabCase("helloWorld")
PRINT Viper.String.ScreamingSnake("helloWorld")

PRINT "--- Search ---"
PRINT Viper.String.IndexOf("abcde", "cd")
PRINT Viper.String.LastIndexOf("abcabc", "bc")
PRINT Viper.String.Has("hello", "ell")
PRINT Viper.String.StartsWith("hello", "hel")
PRINT Viper.String.EndsWith("hello", "llo")
PRINT Viper.String.Count("banana", "an")

PRINT "--- Transform ---"
PRINT Viper.String.Flip("hello")
PRINT Viper.String.Trim("  hello  ")
PRINT Viper.String.TrimStart("  hello  ")
PRINT Viper.String.TrimEnd("  hello  ")
PRINT Viper.String.PadLeft("42", 5, "0")
PRINT Viper.String.PadRight("hi", 5, ".")
PRINT Viper.String.Repeat("ab", 3)
PRINT Viper.String.Replace("hello world", "world", "viper")
PRINT Viper.String.RemovePrefix("hello", "hel")
PRINT Viper.String.RemoveSuffix("hello", "llo")
PRINT Viper.String.Slug("Hello World!")

PRINT "--- Info ---"
PRINT Viper.String.Equals("abc", "abc")
PRINT Viper.String.Equals("abc", "ABC")
PRINT Viper.String.Cmp("abc", "def")
PRINT Viper.String.CmpNoCase("ABC", "abc")

PRINT "--- Distance ---"
PRINT Viper.String.Levenshtein("kitten", "sitting")
PRINT Viper.String.Hamming("karolin", "kathrin")

PRINT "done"
END
