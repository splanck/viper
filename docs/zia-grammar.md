---
status: active
audience: public
last-verified: 2026-06-11
---

# Zia Grammar (EBNF)

This document is the formal grammar for the Zia language, in EBNF. It is
descriptive: the hand-written recursive-descent parser in
`src/frontends/zia/Parser_*.cpp` is the implementation of record, and the
[Zia Reference](zia-reference.md) explains semantics. This grammar exists so
tools and AI assistants can reason about Zia syntax without reverse-engineering
the parser.

Notation: `|` alternatives, `[x]` optional, `{x}` zero-or-more, `(x)` grouping,
`"x"` literal terminals. Productions named in `PascalCase`; token-level rules
in `UPPER_CASE`.

---

## Compilation Unit

```ebnf
CompilationUnit  = ModuleDecl { Bind } { TopLevelDecl } ;

ModuleDecl       = "module" IDENT ";" ;

Bind             = "bind" BindTarget [ "as" IDENT ] [ BindList ] ";" ;
BindTarget       = STRING_LIT            (* file bind: "./utils" — .zia is implied *)
                 | QualifiedName ;       (* namespace bind: Viper.Terminal *)
BindList         = "{" IDENT { "," IDENT } [ "," ] "}" ;

QualifiedName    = IDENT { "." IDENT } ;
```

## Top-Level Declarations

```ebnf
TopLevelDecl     = [ Visibility ] ( FuncDecl
                                  | ClassDecl
                                  | StructDecl
                                  | InterfaceDecl
                                  | EnumDecl
                                  | TypeAliasDecl
                                  | VarDecl ) ;

Visibility       = "expose" | "hide" | "public" | "private" ;

TypeAliasDecl    = "type" IDENT "=" Type ";" ;
```

### Functions

```ebnf
FuncDecl         = [ "async" ] "func" IDENT [ GenericParams ]
                   "(" [ ParamList ] ")" [ "->" Type ] Block ;

GenericParams    = "[" GenericParam { "," GenericParam } "]" ;
GenericParam     = IDENT [ ":" Type ] ;          (* constraint, e.g. T: Comparable *)

ParamList        = Param { "," Param } ;
Param            = IDENT ":" Type ;
```

### Classes, Structs, Interfaces

```ebnf
ClassDecl        = "class" IDENT [ GenericParams ]
                   [ "extends" Type ] [ "implements" TypeList ] ClassBody ;
StructDecl       = "struct" IDENT [ GenericParams ] [ "implements" TypeList ] ClassBody ;
InterfaceDecl    = "interface" IDENT [ GenericParams ] [ "extends" TypeList ] InterfaceBody ;

TypeList         = Type { "," Type } ;

ClassBody        = "{" { MemberDecl } "}" ;
MemberDecl       = [ Visibility ] [ "static" ] ( FieldDecl
                                               | MethodDecl
                                               | InitDecl
                                               | DeinitDecl
                                               | PropertyDecl ) ;

FieldDecl        = [ "weak" ] ( "var" | "final" | "let" ) IDENT [ ":" Type ] [ "=" Expr ] ";"
                 | [ "weak" ] Type IDENT [ "=" Expr ] ";" ;       (* type-first form *)

MethodDecl       = [ "override" ] [ "async" ] "func" IDENT [ GenericParams ]
                   "(" [ ParamList ] ")" [ "->" Type ] Block ;

InitDecl         = "func" "init" "(" [ ParamList ] ")" Block ;
DeinitDecl       = "deinit" Block ;

PropertyDecl     = "property" IDENT ":" Type "{" AccessorList "}" ;
AccessorList     = Accessor { Accessor } ;

InterfaceBody    = "{" { InterfaceMember } "}" ;
InterfaceMember  = "func" IDENT "(" [ ParamList ] ")" [ "->" Type ] ( ";" | Block ) ;
                   (* `;` = abstract; Block = default implementation *)
```

### Enums

```ebnf
EnumDecl         = "enum" IDENT "{" EnumVariant { "," EnumVariant } [ "," ] "}" ;
EnumVariant      = IDENT [ "=" INT_LIT ] ;
```

## Types

```ebnf
Type             = NonOptionalType [ "?" ] ;
NonOptionalType  = QualifiedName [ GenericArgs ]
                 | CollectionType
                 | TupleType
                 | FunctionType ;

GenericArgs      = "[" Type { "," Type } "]" ;

CollectionType   = "List" "[" Type "]"
                 | "Map" "[" Type "," Type "]"
                 | "Set" "[" Type "]" ;

TupleType        = "(" Type "," Type { "," Type } ")" ;

FunctionType     = "(" [ Type { "," Type } ] ")" "->" Type ;
```

Built-in scalar type names: `Boolean`, `Integer`, `Number`, `Byte`, `String`,
`Any`, `Never`, `Unit`. Lowercase and historical aliases are accepted
(`int`/`integer`, `bool`/`Bool`, `double`/`float`, `string`, `byte`, `unit`,
`void`, `error`, `any`, `never`).

## Statements

```ebnf
Block            = "{" { Stmt } "}" ;

Stmt             = VarDecl
                 | IfStmt
                 | WhileStmt
                 | ForStmt
                 | MatchStmt
                 | GuardStmt
                 | TryStmt
                 | DeferStmt
                 | ReturnStmt
                 | BreakStmt
                 | ContinueStmt
                 | ThrowStmt
                 | Block
                 | ExprStmt ;

VarDecl          = ( "var" | "final" | "let" ) Binding [ ":" Type ] [ "=" Expr ] ";" ;
Binding          = IDENT
                 | "(" IDENT { "," IDENT } ")" ;   (* tuple destructuring *)

IfStmt           = "if" Condition Block [ "else" ( IfStmt | Block ) ] ;
WhileStmt        = "while" Condition Block ;
Condition        = Expr | "(" Expr ")" ;           (* parentheses optional *)

ForStmt          = "for" Binding "in" Expr Block ; (* iterable: range, collection *)

GuardStmt        = "guard" Condition "else" Stmt ;

MatchStmt        = "match" Expr "{" { MatchArm } "}" ;
MatchArm         = Pattern [ "if" Expr ] "=>" ( Block | Expr ";" ) ;

TryStmt          = "try" Block CatchClauses [ "finally" Block ]
                 | "try" Block "finally" Block ;
CatchClauses     = { "catch" [ "(" IDENT [ ":" IDENT ] ")" ] Block } ;
                   (* typed catches first; catch-all last *)

DeferStmt        = "defer" ( Block | ExprStmt ) ;
ReturnStmt       = "return" [ Expr ] ";" ;
BreakStmt        = "break" ";" ;
ContinueStmt     = "continue" ";" ;
ThrowStmt        = "throw" [ Expr ] ";" ;          (* bare `throw;` rethrows *)
ExprStmt         = Expr ";" ;
```

Assignment is an expression-statement form: `Lvalue AssignOp Expr ";"` with
`AssignOp = "=" | "+=" | "-=" | "*=" | "/=" | "%="`. Valid lvalues are
identifiers, member accesses, and index expressions.

## Patterns

```ebnf
Pattern          = OrPattern ;
OrPattern        = PrimaryPattern { "|" PrimaryPattern } ;
PrimaryPattern   = "_"                              (* wildcard *)
                 | Literal                          (* 1, "x", true, null *)
                 | QualifiedName                    (* enum variant: Color.Red *)
                 | IDENT                            (* binding *)
                 | "(" Pattern { "," Pattern } ")"  (* tuple *)
                 | QualifiedName "(" [ Pattern { "," Pattern } ] ")" ;  (* constructor *)
```

## Expressions

Binary operator precedence, lowest to highest (all left-associative unless
noted):

| Level | Operators |
|-------|-----------|
| 1 | `? :` ternary (right-assoc); `if cond { a } else { b }` if-expression |
| 2 | `or` |
| 3 | `and` |
| 4 | `==` `!=` |
| 5 | `<` `<=` `>` `>=` `is` |
| 6 | `..` `..=` (ranges) |
| 7 | `\|` (bitwise or), `^` (bitwise xor) |
| 8 | `&` (bitwise and) |
| 9 | `<<` `>>` |
| 10 | `+` `-` |
| 11 | `*` `/` `%` |
| 12 | unary `-` `not` `!` |
| 13 | postfix: call `(...)`, index `[...]`, member `.`, optional-chain `?.`, force-unwrap `!`, try-propagate `?` |

```ebnf
Expr             = TernaryExpr ;
TernaryExpr      = NullCoalesce [ "?" Expr ":" Expr ] ;
NullCoalesce     = BinaryExpr { "??" BinaryExpr } ;
BinaryExpr       = (* precedence-climbing over the table above *) ;

UnaryExpr        = ( "-" | "not" | "!" ) UnaryExpr | PostfixExpr ;
PostfixExpr      = PrimaryExpr { Postfix } ;
Postfix          = "(" [ ArgList ] ")"          (* call *)
                 | "[" Expr "]"                 (* index *)
                 | "." IDENT                    (* member *)
                 | "?." IDENT                   (* optional chain *)
                 | "!"                          (* force unwrap *)
                 | "?" ;                        (* try-propagation *)
ArgList          = Expr { "," Expr } ;

PrimaryExpr      = Literal
                 | IDENT
                 | "self" | "super"
                 | "new" QualifiedName "(" [ ArgList ] ")"
                 | "(" Expr ")"
                 | TupleExpr
                 | ListLiteral | MapLiteral | SetLiteral
                 | LambdaExpr
                 | FuncRef
                 | IfExpr
                 | MatchExpr
                 | "await" Expr ;

TupleExpr        = "(" Expr "," Expr { "," Expr } ")" ;
ListLiteral      = "[" [ Expr { "," Expr } [ "," ] ] "]" ;
MapLiteral       = "{" [ Expr ":" Expr { "," Expr ":" Expr } [ "," ] ] "}" ;
SetLiteral       = "{" Expr { "," Expr } [ "," ] "}" ;

LambdaExpr       = "(" [ ParamList ] ")" "=>" ( Expr | Block ) ;
FuncRef          = "&" QualifiedName ;

IfExpr           = "if" Condition Block "else" Block ;   (* both branches, same type *)
MatchExpr        = "match" Expr "{" { MatchArm } "}" ;

RangeExpr        = Expr ".." Expr | Expr "..=" Expr ;    (* chainable: .rev(), .step(n) *)
```

## Lexical Structure

```ebnf
IDENT            = ( LETTER | "_" ) { LETTER | DIGIT | "_" } ;
INT_LIT          = DIGIT { DIGIT } | "0x" HEXDIGIT { HEXDIGIT } | "0b" BINDIGIT { BINDIGIT } ;
NUM_LIT          = DIGIT { DIGIT } "." DIGIT { DIGIT } [ Exponent ] ;
STRING_LIT       = '"' { CHAR | EscapeSeq | Interpolation } '"'
                 | '"""' { ANY } '"""' ;                 (* multi-line *)
Interpolation    = "${" Expr "}" ;
EscapeSeq        = "\n" | "\t" | "\r" | "\\" | "\"" | "\'" | "\0"
                 | "\x" HEXDIGIT HEXDIGIT | "\u" HEXDIGIT HEXDIGIT HEXDIGIT HEXDIGIT ;
Literal          = INT_LIT | NUM_LIT | STRING_LIT | "true" | "false" | "null" ;

LineComment      = "//" { ANY-except-newline } ;
BlockComment     = "/*" { ANY } "*/" ;                   (* nestable *)
DocComment       = "///" { ANY-except-newline } ;
```

### Reserved Words

```
and as async await bind break catch class continue defer deinit else enum
error escape export expose extends false final finally for foreign func guard
hide if implements in interface is let match module namespace new not null
or override private property public return self static struct super throw
true try type var weak while
```

---

## Cross-References

- Semantics and worked examples: [Zia Reference](zia-reference.md)
- Tutorial: [Zia Getting Started](zia-getting-started.md)
- BASIC equivalent: [BASIC Grammar Notes](basic-grammar.md)
- Parser implementation: `src/frontends/zia/Parser_Decl.cpp`,
  `Parser_Stmt.cpp`, `Parser_Expr.cpp`, `Parser_Expr_Primary.cpp`,
  `Parser_Expr_Pattern.cpp`, `Parser_Type.cpp`

If this grammar and the parser disagree, the parser wins — please file the
discrepancy so this document can be corrected.
