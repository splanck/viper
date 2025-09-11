<!--
SPDX-License-Identifier: MIT
File: docs/reference/basic-language.md
Purpose: Reference for BASIC intrinsic functions.
-->

# BASIC Intrinsic Functions

> **Compatibility:** `UCASE$`/`LCASE$` perform ASCII-only case mapping. `LTRIM$`, `RTRIM$`, and `TRIM$` treat both space and tab as whitespace.

## Indexing and Substrings {#indexing-and-substrings}

String positions are 1-based. Substring functions interpret the starting position as 1 = first character. When a length is supplied, it counts characters from the starting position.

## Functions

### LEN

**Syntax**
```
LEN(string$)
```

**Arguments**
- `string$`: input string.

**Returns**
- Length of `string$` in characters.

**Notes**
- Returns 0 for an empty string.

**Example** ([`examples/basic/strings/len.bas`](../../examples/basic/strings/len.bas))
```basic
10 PRINT LEN("HELLO")
20 PRINT LEN("")
30 PRINT LEN("A")
```
Output
```
5
0
1
```

### LEFT$

**Syntax**
```
LEFT$(string$, count)
```

**Arguments**
- `string$`: input string.
- `count`: number of characters to take from the start.

**Returns**
- Leftmost `count` characters.

**Notes**
- `count` less than 0 yields an empty string.

**Example** ([`examples/basic/strings/left_right.bas`](../../examples/basic/strings/left_right.bas))
```basic
10 LET S$ = "HELLO"
20 PRINT LEFT$(S$,2)
30 PRINT RIGHT$(S$,3)
40 PRINT LEFT$(S$,1)
```
Output
```
HE
LLO
H
```

### RIGHT$

**Syntax**
```
RIGHT$(string$, count)
```

**Arguments**
- `string$`: input string.
- `count`: number of characters to take from the end.

**Returns**
- Rightmost `count` characters.

**Notes**
- `count` less than 0 yields an empty string.

**Example** ([`examples/basic/strings/left_right.bas`](../../examples/basic/strings/left_right.bas))
```basic
10 LET S$ = "HELLO"
20 PRINT LEFT$(S$,2)
30 PRINT RIGHT$(S$,3)
40 PRINT LEFT$(S$,1)
```
Output
```
HE
LLO
H
```

### MID$

**Syntax**
```
MID$(string$, start[, length])
```

**Arguments**
- `string$`: source string.
- `start`: 1-based starting index.
- `length` (optional): number of characters to extract.

**Returns**
- Substring from `start` for `length` characters or to the end.

**Notes**
- See [indexing and substrings](#indexing-and-substrings).
- `start` before 1 or past the end yields an empty string.

**Example** ([`examples/basic/strings/mid.bas`](../../examples/basic/strings/mid.bas))
```basic
10 LET S$ = "ABCDEFG"
20 PRINT MID$(S$,3,2)
30 PRINT MID$(S$,4)
40 PRINT MID$(S$,1,3)
```
Output
```
CD
DEFG
ABC
```

### INSTR

**Syntax**
```
INSTR([start,] haystack$, needle$)
```

**Arguments**
- `start` (optional): 1-based index to begin search.
- `haystack$`: string to search.
- `needle$`: substring to find.

**Returns**
- 1-based index of first occurrence, or 0 if not found.

**Notes**
- See [indexing and substrings](#indexing-and-substrings).
- Search includes `start` position when provided.

**Example** ([`examples/basic/strings/instr.bas`](../../examples/basic/strings/instr.bas))
```basic
10 PRINT INSTR("HELLO","LL")
20 PRINT INSTR(3,"HELLO","L")
30 PRINT INSTR(1,"HI","Z")
```
Output
```
3
3
0
```

### LTRIM$

**Syntax**
```
LTRIM$(string$)
```

**Arguments**
- `string$`: input string.

**Returns**
- String with leading whitespace removed.

**Notes**
- Whitespace includes space and tab.

**Example** ([`examples/basic/strings/trims.bas`](../../examples/basic/strings/trims.bas))
```basic
10 PRINT ":"; LTRIM$("  HI "); ":"
20 PRINT ":"; RTRIM$("  HI "); ":"
30 PRINT ":"; TRIM$("  HI "); ":"
```
Output
```
:HI :
:  HI:
:HI:
```

### RTRIM$

**Syntax**
```
RTRIM$(string$)
```

**Arguments**
- `string$`: input string.

**Returns**
- String with trailing whitespace removed.

**Notes**
- Whitespace includes space and tab.

**Example** ([`examples/basic/strings/trims.bas`](../../examples/basic/strings/trims.bas))
```basic
10 PRINT ":"; LTRIM$("  HI "); ":"
20 PRINT ":"; RTRIM$("  HI "); ":"
30 PRINT ":"; TRIM$("  HI "); ":"
```
Output
```
:HI :
:  HI:
:HI:
```

### TRIM$

**Syntax**
```
TRIM$(string$)
```

**Arguments**
- `string$`: input string.

**Returns**
- String with leading and trailing whitespace removed.

**Notes**
- Whitespace includes space and tab.

**Example** ([`examples/basic/strings/trims.bas`](../../examples/basic/strings/trims.bas))
```basic
10 PRINT ":"; LTRIM$("  HI "); ":"
20 PRINT ":"; RTRIM$("  HI "); ":"
30 PRINT ":"; TRIM$("  HI "); ":"
```
Output
```
:HI :
:  HI:
:HI:
```

### UCASE$

**Syntax**
```
UCASE$(string$)
```

**Arguments**
- `string$`: input string.

**Returns**
- Copy of `string$` with letters converted to uppercase.

**Notes**
- ASCII-only; non-ASCII bytes are unchanged.

**Example** ([`examples/basic/strings/case.bas`](../../examples/basic/strings/case.bas))
```basic
10 PRINT UCASE$("abC")
20 PRINT LCASE$("AbC")
30 PRINT UCASE$("123abc")
```
Output
```
ABC
abc
123ABC
```

### LCASE$

**Syntax**
```
LCASE$(string$)
```

**Arguments**
- `string$`: input string.

**Returns**
- Copy of `string$` with letters converted to lowercase.

**Notes**
- ASCII-only; non-ASCII bytes are unchanged.

**Example** ([`examples/basic/strings/case.bas`](../../examples/basic/strings/case.bas))
```basic
10 PRINT UCASE$("abC")
20 PRINT LCASE$("AbC")
30 PRINT UCASE$("123abc")
```
Output
```
ABC
abc
123ABC
```

### CHR$

**Syntax**
```
CHR$(code)
```

**Arguments**
- `code`: integer ASCII code.

**Returns**
- Single-character string.

**Notes**
- Only values 0–255 are meaningful.

**Example** ([`examples/basic/strings/chr_asc.bas`](../../examples/basic/strings/chr_asc.bas))
```basic
10 PRINT CHR$(65)
20 PRINT ASC("A")
30 PRINT ASC(CHR$(66))
```
Output
```
A
65
66
```

### ASC

**Syntax**
```
ASC(string$)
```

**Arguments**
- `string$`: input string.

**Returns**
- ASCII code of first character.

**Notes**
- Only the first byte of `string$` is used.

**Example** ([`examples/basic/strings/chr_asc.bas`](../../examples/basic/strings/chr_asc.bas))
```basic
10 PRINT CHR$(65)
20 PRINT ASC("A")
30 PRINT ASC(CHR$(66))
```
Output
```
A
65
66
```

### VAL

**Syntax**
```
VAL(string$)
```

**Arguments**
- `string$`: digits optionally prefixed with sign.

**Returns**
- Numeric value parsed from `string$`.

**Notes**
- Parsing stops at first non-digit; result may be int or float.

**Example** ([`examples/basic/strings/val_str.bas`](../../examples/basic/strings/val_str.bas))
```basic
10 PRINT VAL("42")
20 PRINT STR$(42.5)
30 PRINT VAL("-7")
```
Output
```
42
42.5
-7
```

### STR$

**Syntax**
```
STR$(number)
```

**Arguments**
- `number`: numeric value.

**Returns**
- String representation of `number`.

**Notes**
- Uses decimal formatting.

**Example** ([`examples/basic/strings/val_str.bas`](../../examples/basic/strings/val_str.bas))
```basic
10 PRINT VAL("42")
20 PRINT STR$(42.5)
30 PRINT VAL("-7")
```
Output
```
42
42.5
-7
```
