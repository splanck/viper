BASIC v0.1 Language Reference (for this project)
Status: Implemented subset for front-end bring-up
Back end: Lowers to IL v0.1.1 → VM interpreter (native codegen WIP)
1. Goals & Scope
Small, predictable BASIC subset suitable for early IDE/compiler bring-up.
Deterministic semantics that map cleanly to IL and runtime calls.
Feature set chosen to cover: variables, arithmetic, strings, conditionals, loops, simple I/O.
2. Programs & Structure
A program is a sequence of statements separated by newlines or ':' on the same line.
Line numbers are optional; when present they act as labels (targets for GOTO).
Execution starts at the first line (or the first statement if no numbers).
Comments start with ' and run to end of line.
10 PRINT "HELLO"
20 LET X = 2 + 3
30 IF X > 4 THEN PRINT X ELSE PRINT 4
40 END
3. Types
Integer: 64-bit signed (i64 in IL). Literal examples: 0, -12, 42.
Float (optional in v0.1; produced via VAL of string): 64-bit IEEE (f64).
String: UTF-8 sequences. Literal: "text", escapes \" \\ \n \t \xNN.
Boolean: expression result TRUE/FALSE (internally i1), accepted in conditions.
Coercions
Integer + Integer → Integer (wraps on overflow in IL).
LEN, VAL, MID$ provide string↔numeric operations via runtime.
PRINT chooses rt_print_str for strings, rt_print_i64 for integers, rt_print_f64 for floats.
4. Expressions
Precedence (high→low):
()
Unary: NOT, + -
* /
+ - (binary)
Comparisons: = <> < <= > >= (yield Boolean)
AND
OR
Operators:
Arithmetic on integers; / is integer division (traps on div/0).
Comparisons allowed between like types (int with int, str with str using =/<> only).
Logical operators AND/OR/NOT use short-circuit evaluation and return Boolean.
Built-ins (all map to runtime; see §10):
LEN(s$) -> integer
MID$(s$, start, length) -> string (1-based indices; clamped)
VAL(s$) -> integer (traps on invalid; VALF$ for float optional later)
5. Statements
LET var = expr — assign (vars auto-declared on first use)
PRINT expr — output value
IF cond THEN stmt {ELSEIF cond THEN stmt}* [ELSE stmt]
Multi-stmt THEN/ELSE blocks: chain multiple PRINT/LET/... on subsequent lines or use ':' separators.
WHILE cond ... WEND
FOR var = start TO end [STEP s] ... NEXT var
GOTO lineNumber
END — terminate program
INPUT var$ (optional if runtime wired) — reads a line into a string variable
6. Variables & Names
Names: [A-Za-z][A-Za-z0-9_]* with optional $ suffix for strings (NAME$).
Type inference:
$ suffix → string
otherwise integer unless assigned from VALF$(…) (future).
All variables are function-local to @main in v0.1.
7. Errors
Division by zero, invalid VAL, out-of-bounds MID$ length/start after clamping → runtime trap with message.
Type mismatch in comparisons/operations → compile-time (lowering) error.
8. Diagnostics
Errors use standardized codes prefixed with B. Messages show the source line and a caret.
10 LET X = 1 +
            ^
B0001: expected expression.
Runtime traps use codes like B0002 for division by zero.
9. Grammar (informal)
program     ::= (line | stmt)* EOF
line        ::= (NUMBER)? stmt (":" stmt)* NEWLINE
stmt        ::= "LET" ident "=" expr
             | "PRINT" expr
             | "IF" expr "THEN" stmt ("ELSEIF" expr "THEN" stmt)* ("ELSE" stmt)?
             | "WHILE" expr (NEWLINE|":") stmt* "WEND"
             | "FOR" ident "=" expr "TO" expr ("STEP" expr)? (NEWLINE|":") stmt* "NEXT" ident
             | "GOTO" NUMBER
             | "END"
             | "INPUT" ident
expr        ::= term (("+"|"-") term)*
term        ::= factor (("*"|"/") factor)*
factor      ::= NUMBER | STRING | ident | "(" expr ")" | ("+"|"-") factor | "NOT" factor
ident       ::= NAME | NAME "$"
10. Mapping to IL & Runtime
BASIC | IL pattern | Runtime
---- | ---- | ----
PRINT "X" | %s = const_str @.L; call @rt_print_str(%s) | rt_print_str(str)
PRINT X | %v = load i64, %slotX; call @rt_print_i64(%v) | rt_print_i64(i64)
LET X = A + B | load A; load B; %c = add %a,%b; store X,%c | —
IF C THEN … ELSE … | %p = …cmp…; cbr %p, then, else | —
WHILE C … WEND | br loop_head; cbr cond, loop_body, done | —
LEN(S$) | call @rt_len(%s) | rt_len(str)->i64
MID$(S$,i,l) | call @rt_substr(%s, i-1, l) | rt_substr(str,i64,i64)->str
VAL(S$) | call @rt_to_int(%s) | rt_to_int(str)->i64
INPUT A$ | %s = call @rt_input_line(); store A$, %s | rt_input_line()->str
Indexing: BASIC’s 1-based indices are lowered to 0-based for runtime calls (subtract 1).
11. Examples
See /docs/examples/basic/ and the IL equivalents in /docs/examples/il/.
