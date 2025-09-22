<!--
File: docs/dev/frontends/parser.md
Purpose: Notes on BASIC parser and comment lexing.
-->

# BASIC Parser

The lexer recognizes single-line comments beginning with an apostrophe `'`
or the keyword `REM`. `REM` is case-insensitive and only treated as a
comment when it appears at the start of a line or after whitespace.
Characters are skipped until the end of the line, and no comment tokens
are produced. Line and column counters continue to advance so subsequent
tokens report accurate locations.
