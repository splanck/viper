# ZIA-FEAT-06: Type Aliases `type Name = Type;`

## Context
The reference documents type aliases: `type UserId = String;` creates a
compile-time alias that resolves to the target type. Currently the `type`
keyword doesn't exist. This is purely a compile-time feature — no IL or
runtime changes needed.

**Complexity: M** | **Risk: Low** (no runtime impact, sema-only resolution)

## Design

### Syntax
```zia
type UserId = String;
type StringList = List[String];
type Callback = (Integer) -> Boolean;
type Pair[T] = (T, T);          // generic alias (stretch goal)
```

### Semantics
- Type aliases are transparent — `UserId` and `String` are interchangeable
- The alias resolves during type resolution in sema (before lowering)
- Aliases can reference other aliases (resolved transitively)
- Circular aliases are an error

## Files to Modify

### 1. Token.hpp (~line 167, type definition keywords section)
Add `KwType` near `KwStruct`/`KwClass`/`KwEnum`/`KwInterface`:
```cpp
KwType,  // type alias declaration
```

Ensure it falls within the keyword range checked by `Token::isKeyword()`.

### 2. Lexer.cpp
- In `kKeywordTable` (sorted array, ~line 276): add `{"type", TokenKind::KwType}`
  between "try" and "var". Increment array size.
- In `tokenKindToString`: add `case TokenKind::KwType: return "type";`

### 3. AST_Decl.hpp (~line 105, DeclKind enum)
Add:
```cpp
TypeAlias,  ///< type alias: type Name = TargetType;
```

Add new AST node struct:
```cpp
struct TypeAliasDecl : Decl {
    std::string name;
    TypePtr targetType;
    SourceLoc loc;

    TypeAliasDecl(SourceLoc l, std::string n, TypePtr t)
        : Decl(DeclKind::TypeAlias, l), name(std::move(n)),
          targetType(std::move(t)) {}
};
```

### 4. Parser.hpp
Declare:
```cpp
DeclPtr parseTypeAlias();
```

### 5. Parser_Decl.cpp
In the `parseDeclaration()` dispatcher, add a case:
```cpp
case TokenKind::KwType:
    return parseTypeAlias();
```

Implement:
```cpp
DeclPtr Parser::parseTypeAlias() {
    advance(); // consume 'type'
    SourceLoc loc = prev().loc;

    if (!check(TokenKind::Identifier)) {
        error("expected type alias name");
        return nullptr;
    }
    std::string name = advance().text;

    if (!expect(TokenKind::Equal, "=")) return nullptr;

    TypePtr target = parseType();
    if (!target) return nullptr;

    if (!expect(TokenKind::Semicolon, ";")) return nullptr;

    return std::make_unique<TypeAliasDecl>(loc, std::move(name), std::move(target));
}
```

### 6. Sema.hpp / Sema_Decl.cpp
Add a member:
```cpp
std::unordered_map<std::string, TypeRef> typeAliases_;
```

In `analyzeDecl()`, add `DeclKind::TypeAlias` case:
```cpp
case DeclKind::TypeAlias: {
    auto *alias = static_cast<TypeAliasDecl *>(decl);
    TypeRef resolved = resolveTypeNode(alias->targetType.get());
    if (resolved) {
        typeAliases_[alias->name] = resolved;
    } else {
        error(alias->loc, "Cannot resolve type alias target");
    }
    break;
}
```

### 7. Sema_TypeResolution.cpp (~line 146, resolveNamedType)
Before the "Unknown type" error, check aliases:
```cpp
auto aliasIt = typeAliases_.find(name);
if (aliasIt != typeAliases_.end()) {
    return aliasIt->second;
}
```

### 8. Lowerer_Decl.cpp
In `lowerDecl()`, add no-op case:
```cpp
case DeclKind::TypeAlias:
    break;  // Type aliases are fully resolved at sema time
```

## Verification
```zia
type Ints = List[Integer];
type Predicate = (Integer) -> Boolean;

func filter(nums: Ints, pred: Predicate) -> Ints {
    var result: Ints = [];
    for n in nums {
        if pred(n) { result.Push(n); }
    }
    return result;
}

func start() {
    var nums: Ints = [1, 2, 3, 4, 5, 6];
    var evens = filter(nums, (x: Integer) => x % 2 == 0);
    for n in evens { SayInt(n); }  // 2, 4, 6
}
```
Run full test suite: `ctest --test-dir build --output-on-failure`
