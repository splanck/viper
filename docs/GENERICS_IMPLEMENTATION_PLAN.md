# Zia Generics Implementation Plan

## Executive Summary

This document outlines a comprehensive plan to implement full generics support in the Zia language. The implementation builds on existing infrastructure (generic parameter parsing, TypeParam kind, typeArgs storage) and fills the semantic analysis and lowering gaps required for user-defined generic types and functions.

**Current State**: Parser supports generic syntax; semantic analysis partially handles built-in collections; user-defined generics are parsed but not instantiated.

**Target State**: Full monomorphization-based generics with type inference, supporting user-defined generic types, functions, and methods.

---

## 1. Architecture Overview

### 1.1 Type System Layers

```
┌─────────────────────────────────────────────────────────────┐
│                      Source Code                             │
│    value Box[T] { T contents; }                             │
│    var b: Box[Integer] = Box(42);                           │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    AST (Parser)                              │
│    GenericType { name: "Box", args: [NamedType("Integer")] }│
│    ValueDecl { name: "Box", genericParams: ["T"], ... }     │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              Semantic Types (Sema)                           │
│    ViperType { kind: Value, name: "Box",                    │
│                typeArgs: [TypeRef(Integer)] }               │
│    + TypeParamSubstitution: { "T" → Integer }               │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              Monomorphization (Lowerer)                      │
│    Generate specialized: Box$Integer                         │
│    - Fields use concrete Integer type                        │
│    - Methods specialized for Integer                         │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                     IL Code                                  │
│    struct Box$Integer { i64 contents; }                     │
│    func Box$Integer.init(i64) -> ptr                        │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **Instantiation Strategy** | Monomorphization | Generates specialized code per type argument combination; best performance; matches Zia's static typing |
| **Type Inference** | Bidirectional | Infer from arguments (bottom-up) and expected type (top-down) |
| **Constraint System** | Deferred to v0.2 | v0.1 focuses on unconstrained generics per spec |
| **IL Representation** | Name mangling | `Box[Integer]` → `Box$Integer` in IL |
| **Generic Caching** | Per-module cache | Avoid re-analyzing same instantiation |

---

## 2. Implementation Phases

### Phase 1: Type Parameter Substitution Infrastructure
**Estimated Complexity**: Medium | **Files**: 4 | **New Code**: ~200 lines

### Phase 2: Generic Type Instantiation
**Estimated Complexity**: High | **Files**: 6 | **New Code**: ~400 lines

### Phase 3: Generic Function Support
**Estimated Complexity**: High | **Files**: 5 | **New Code**: ~350 lines

### Phase 4: Lowering & Monomorphization
**Estimated Complexity**: Very High | **Files**: 8 | **New Code**: ~600 lines

### Phase 5: Type Inference Enhancement
**Estimated Complexity**: Medium | **Files**: 3 | **New Code**: ~250 lines

---

## 3. Phase 1: Type Parameter Substitution Infrastructure

### 3.1 Objective
Enable semantic analysis to substitute type parameters with concrete types during type resolution.

### 3.2 Changes

#### 3.2.1 Sema.hpp - Add Substitution Context

```cpp
// Add to Sema class (around line 150)
private:
    /// Active type parameter substitutions for current generic context
    /// Maps type parameter names (e.g., "T") to concrete types
    std::vector<std::map<std::string, TypeRef>> typeParamStack_;

public:
    /// Push a new substitution scope (entering generic context)
    void pushTypeParams(const std::map<std::string, TypeRef>& substitutions);

    /// Pop substitution scope (leaving generic context)
    void popTypeParams();

    /// Look up a type parameter in current context
    TypeRef lookupTypeParam(const std::string& name) const;

    /// Check if currently inside a generic context
    bool inGenericContext() const { return !typeParamStack_.empty(); }
```

#### 3.2.2 Sema.cpp - Implement Substitution Methods

```cpp
void Sema::pushTypeParams(const std::map<std::string, TypeRef>& substitutions)
{
    typeParamStack_.push_back(substitutions);
}

void Sema::popTypeParams()
{
    assert(!typeParamStack_.empty() && "Unbalanced type param stack");
    typeParamStack_.pop_back();
}

TypeRef Sema::lookupTypeParam(const std::string& name) const
{
    // Search from innermost to outermost scope
    for (auto it = typeParamStack_.rbegin(); it != typeParamStack_.rend(); ++it)
    {
        auto found = it->find(name);
        if (found != it->end())
            return found->second;
    }
    return nullptr;  // Not found - remains unsubstituted
}
```

#### 3.2.3 Sema.cpp - Modify resolveTypeNode()

```cpp
// In resolveTypeNode(), handle TypeKind::Named (around line 340)
TypeRef Sema::resolveTypeNode(TypeNode* node)
{
    if (!node) return types::unknown();

    switch (node->kind)
    {
        case TypeKind::Named:
        {
            auto* named = static_cast<NamedType*>(node);

            // NEW: Check if this is a type parameter in current context
            if (TypeRef substituted = lookupTypeParam(named->name))
            {
                return substituted;
            }

            // Existing resolution logic...
            return resolveNamedType(named->name);
        }
        // ... rest unchanged
    }
}
```

### 3.3 Test Cases

```zia
// test_generics_substitution.zia

// Test 1: Simple type parameter substitution
value Wrapper[T] {
    T value;
}
var w: Wrapper[Integer];  // T should substitute to Integer

// Test 2: Nested substitution
value Pair[A, B] {
    A first;
    B second;
}
var p: Pair[String, Integer];  // A→String, B→Integer

// Test 3: Type parameter in function signature
func identity[T](x: T) -> T {
    return x;
}
var result = identity[Integer](42);  // T→Integer
```

---

## 4. Phase 2: Generic Type Instantiation

### 4.1 Objective
When a generic type is used with concrete type arguments (e.g., `Box[Integer]`), create a specialized version of that type.

### 4.2 Changes

#### 4.2.1 Sema.hpp - Generic Instance Registry

```cpp
// Add to Sema class
private:
    /// Cache of instantiated generic types
    /// Key: "TypeName$Arg1$Arg2", Value: Instantiated TypeRef
    std::map<std::string, TypeRef> genericInstances_;

    /// Original generic type declarations (uninstantiated)
    /// Key: Type name, Value: AST declaration
    std::map<std::string, DeclPtr> genericTypeDecls_;

public:
    /// Register a generic type declaration for later instantiation
    void registerGenericType(const std::string& name, DeclPtr decl);

    /// Instantiate a generic type with concrete arguments
    TypeRef instantiateGenericType(const std::string& name,
                                   const std::vector<TypeRef>& args);

    /// Generate mangled name for instantiation
    static std::string mangleGenericName(const std::string& base,
                                         const std::vector<TypeRef>& args);
```

#### 4.2.2 Sema.cpp - Implement Instantiation

```cpp
std::string Sema::mangleGenericName(const std::string& base,
                                    const std::vector<TypeRef>& args)
{
    std::string result = base;
    for (const auto& arg : args)
    {
        result += "$";
        result += arg->name.empty() ? typeKindToString(arg->kind) : arg->name;
    }
    return result;
}

TypeRef Sema::instantiateGenericType(const std::string& name,
                                     const std::vector<TypeRef>& args)
{
    // Check cache first
    std::string mangledName = mangleGenericName(name, args);
    auto cached = genericInstances_.find(mangledName);
    if (cached != genericInstances_.end())
        return cached->second;

    // Find original generic declaration
    auto declIt = genericTypeDecls_.find(name);
    if (declIt == genericTypeDecls_.end())
    {
        error({}, "Unknown generic type: " + name);
        return types::unknown();
    }

    // Build substitution map
    std::map<std::string, TypeRef> substitutions;
    const auto& genericParams = getGenericParams(declIt->second.get());

    if (args.size() != genericParams.size())
    {
        error({}, "Generic type " + name + " expects " +
              std::to_string(genericParams.size()) + " type arguments, got " +
              std::to_string(args.size()));
        return types::unknown();
    }

    for (size_t i = 0; i < genericParams.size(); ++i)
    {
        substitutions[genericParams[i]] = args[i];
    }

    // Push substitution context and re-analyze type
    pushTypeParams(substitutions);
    TypeRef instantiated = analyzeGenericTypeBody(declIt->second.get(), mangledName);
    popTypeParams();

    // Cache and return
    genericInstances_[mangledName] = instantiated;
    return instantiated;
}
```

#### 4.2.3 Sema_Decl.cpp - Register Generic Types

```cpp
// In analyzeValueDecl() and analyzeEntityDecl()
void Sema::analyzeValueDecl(ValueDecl* decl)
{
    // If this is a generic type, register for later instantiation
    if (!decl->genericParams.empty())
    {
        registerGenericType(decl->name, decl->shared_from_this());

        // Create uninstantiated type with TypeParam placeholders
        std::vector<TypeRef> paramTypes;
        for (const auto& param : decl->genericParams)
        {
            paramTypes.push_back(types::typeParam(param));
        }

        TypeRef genericType = std::make_shared<ViperType>(
            TypeKindSem::Value, decl->name, paramTypes);
        registerType(decl->name, genericType);
        return;  // Don't analyze body yet - wait for instantiation
    }

    // Non-generic type: analyze immediately (existing logic)
    // ...
}
```

#### 4.2.4 Sema.cpp - Modify resolveTypeNode() for Generic Types

```cpp
// In resolveTypeNode(), TypeKind::Generic case (around line 350)
case TypeKind::Generic:
{
    auto* generic = static_cast<GenericType*>(node);

    // Resolve type arguments first
    std::vector<TypeRef> resolvedArgs;
    for (const auto& arg : generic->args)
    {
        resolvedArgs.push_back(resolveTypeNode(arg.get()));
    }

    // Handle built-in generics (existing logic)
    if (generic->name == "List")
        return types::list(resolvedArgs.empty() ? types::unknown() : resolvedArgs[0]);
    if (generic->name == "Map")
        return types::map(resolvedArgs.size() > 0 ? resolvedArgs[0] : types::string(),
                         resolvedArgs.size() > 1 ? resolvedArgs[1] : types::unknown());
    // ... other built-ins

    // NEW: Handle user-defined generic types
    if (genericTypeDecls_.count(generic->name))
    {
        return instantiateGenericType(generic->name, resolvedArgs);
    }

    // Fallback: create generic type reference
    return std::make_shared<ViperType>(TypeKindSem::Unknown, generic->name, resolvedArgs);
}
```

### 4.3 Test Cases

```zia
// test_generics_instantiation.zia

// Test 1: Simple generic value type
value Box[T] {
    T contents;

    func init(value: T) {
        contents = value;
    }

    func get() -> T {
        return contents;
    }
}

var intBox: Box[Integer] = Box[Integer](42);
var strBox: Box[String] = Box[String]("hello");

if (intBox.get() == 42) {
    Viper.Terminal.Say("RESULT: ok");
}

// Test 2: Generic with multiple parameters
value Pair[A, B] {
    A first;
    B second;
}

var pair: Pair[String, Integer] = Pair[String, Integer]("age", 25);

// Test 3: Nested generics
var boxedList: Box[List[Integer]] = Box[List[Integer]]([1, 2, 3]);
```

---

## 5. Phase 3: Generic Function Support

### 5.1 Objective
Support generic functions with type parameter inference from arguments.

### 5.2 Changes

#### 5.2.1 Sema.hpp - Generic Function Registry

```cpp
// Add to Sema class
private:
    /// Generic function declarations
    std::map<std::string, FunctionDecl*> genericFuncDecls_;

    /// Instantiated generic functions
    /// Key: "funcName$Arg1$Arg2", Value: Symbol with specialized signature
    std::map<std::string, Symbol*> genericFuncInstances_;

public:
    /// Register a generic function declaration
    void registerGenericFunction(const std::string& name, FunctionDecl* decl);

    /// Instantiate a generic function with inferred/explicit type arguments
    Symbol* instantiateGenericFunction(const std::string& name,
                                       const std::vector<TypeRef>& typeArgs);

    /// Infer type arguments from call arguments
    std::vector<TypeRef> inferTypeArguments(FunctionDecl* genericDecl,
                                            const std::vector<TypeRef>& argTypes);
```

#### 5.2.2 Sema_Expr.cpp - Generic Function Call Resolution

```cpp
// In analyzeCall(), when resolving function (around line 400)
TypeRef Sema::analyzeCall(CallExpr* expr)
{
    // ... existing callee resolution ...

    // Check if callee is a generic function
    if (auto* ident = dynamic_cast<IdentExpr*>(expr->callee.get()))
    {
        auto genericIt = genericFuncDecls_.find(ident->name);
        if (genericIt != genericFuncDecls_.end())
        {
            FunctionDecl* genericDecl = genericIt->second;

            // Analyze argument types first
            std::vector<TypeRef> argTypes;
            for (const auto& arg : expr->args)
            {
                argTypes.push_back(analyzeExpr(arg.value.get()));
            }

            // Get explicit type arguments if provided
            std::vector<TypeRef> explicitTypeArgs;
            if (expr->typeArgs)  // NEW: TypeArgs on call expression
            {
                for (const auto& typeArg : expr->typeArgs->args)
                {
                    explicitTypeArgs.push_back(resolveTypeNode(typeArg.get()));
                }
            }

            // Infer missing type arguments
            std::vector<TypeRef> typeArgs = explicitTypeArgs.empty()
                ? inferTypeArguments(genericDecl, argTypes)
                : explicitTypeArgs;

            // Instantiate the function
            Symbol* specialized = instantiateGenericFunction(ident->name, typeArgs);
            if (!specialized)
                return types::unknown();

            // Return the specialized function's return type
            if (specialized->type && specialized->type->kind == TypeKindSem::Function)
                return specialized->type->returnType();
            return specialized->type;
        }
    }

    // ... existing non-generic call resolution ...
}
```

#### 5.2.3 Type Inference Algorithm

```cpp
std::vector<TypeRef> Sema::inferTypeArguments(FunctionDecl* genericDecl,
                                               const std::vector<TypeRef>& argTypes)
{
    std::map<std::string, TypeRef> inferred;
    const auto& params = genericDecl->params;

    // Match each argument type against parameter type
    for (size_t i = 0; i < std::min(params.size(), argTypes.size()); ++i)
    {
        TypeRef paramType = resolveTypeNode(params[i].type.get());
        TypeRef argType = argTypes[i];

        // If parameter type is a type parameter, infer it
        if (paramType->kind == TypeKindSem::TypeParam)
        {
            const std::string& paramName = paramType->name;
            auto existing = inferred.find(paramName);
            if (existing != inferred.end())
            {
                // Check consistency
                if (!typesEqual(existing->second, argType))
                {
                    error(genericDecl->loc,
                          "Conflicting type inference for " + paramName);
                }
            }
            else
            {
                inferred[paramName] = argType;
            }
        }
        // Handle nested generics: List[T] matched with List[Integer]
        else if (paramType->isGeneric() && argType->isGeneric())
        {
            inferFromGenericTypes(paramType, argType, inferred);
        }
    }

    // Build result vector in declaration order
    std::vector<TypeRef> result;
    for (const auto& genericParam : genericDecl->genericParams)
    {
        auto it = inferred.find(genericParam);
        if (it != inferred.end())
        {
            result.push_back(it->second);
        }
        else
        {
            error(genericDecl->loc,
                  "Cannot infer type argument for " + genericParam);
            result.push_back(types::unknown());
        }
    }

    return result;
}
```

### 5.3 Test Cases

```zia
// test_generics_functions.zia

// Test 1: Simple generic function with inference
func identity[T](value: T) -> T {
    return value;
}

var a = identity(42);        // T inferred as Integer
var b = identity("hello");   // T inferred as String

// Test 2: Multiple type parameters
func swap[A, B](pair: (A, B)) -> (B, A) {
    return (pair.1, pair.0);
}

var swapped = swap((1, "one"));  // A=Integer, B=String

// Test 3: Generic function with generic types
func first[T](list: List[T]) -> T {
    return list.get(0);
}

var nums = [1, 2, 3];
var f = first(nums);  // T inferred as Integer

// Test 4: Explicit type arguments
var x = identity[Number](42);  // Explicit: T=Number (Integer→Number coercion)

// Test 5: Generic method
value Container[T] {
    List[T] items;

    func map[U](transform: func(T) -> U) -> Container[U] {
        var result = Container[U]();
        for item in items {
            result.items.add(transform(item));
        }
        return result;
    }
}

var ints = Container[Integer]();
ints.items = [1, 2, 3];
var strings = ints.map[String](func(x: Integer) -> String {
    return Viper.Convert.ToString_Int(x);
});
```

---

## 6. Phase 4: Lowering & Monomorphization

### 6.1 Objective
Generate specialized IL code for each unique generic instantiation.

### 6.2 Changes

#### 6.2.1 Lowerer.hpp - Monomorphization Tracking

```cpp
// Add to Lowerer class
private:
    /// Track which generic instantiations have been lowered
    /// Key: mangled name (e.g., "Box$Integer"), Value: true if lowered
    std::unordered_set<std::string> loweredInstantiations_;

    /// Queue of pending generic instantiations to lower
    std::queue<std::pair<std::string, std::vector<TypeRef>>> pendingInstantiations_;

    /// Original generic declarations from Sema
    const std::map<std::string, DeclPtr>& genericTypeDecls_;
    const std::map<std::string, FunctionDecl*>& genericFuncDecls_;

public:
    /// Request lowering of a generic instantiation
    void requestInstantiation(const std::string& name,
                              const std::vector<TypeRef>& typeArgs);

    /// Process all pending instantiations
    void lowerPendingInstantiations();

    /// Lower a specific generic type instantiation
    void lowerGenericTypeInstance(const std::string& baseName,
                                  const std::vector<TypeRef>& typeArgs);

    /// Lower a specific generic function instantiation
    void lowerGenericFunctionInstance(const std::string& baseName,
                                      const std::vector<TypeRef>& typeArgs);
```

#### 6.2.2 Lowerer.cpp - Monomorphization Implementation

```cpp
void Lowerer::requestInstantiation(const std::string& name,
                                   const std::vector<TypeRef>& typeArgs)
{
    std::string mangled = Sema::mangleGenericName(name, typeArgs);
    if (loweredInstantiations_.count(mangled) == 0)
    {
        pendingInstantiations_.push({name, typeArgs});
    }
}

void Lowerer::lowerPendingInstantiations()
{
    while (!pendingInstantiations_.empty())
    {
        auto [name, typeArgs] = pendingInstantiations_.front();
        pendingInstantiations_.pop();

        std::string mangled = Sema::mangleGenericName(name, typeArgs);
        if (loweredInstantiations_.count(mangled))
            continue;  // Already lowered (could have been added while processing)

        loweredInstantiations_.insert(mangled);

        // Determine if it's a type or function
        if (genericTypeDecls_.count(name))
        {
            lowerGenericTypeInstance(name, typeArgs);
        }
        else if (genericFuncDecls_.count(name))
        {
            lowerGenericFunctionInstance(name, typeArgs);
        }
    }
}

void Lowerer::lowerGenericTypeInstance(const std::string& baseName,
                                       const std::vector<TypeRef>& typeArgs)
{
    std::string mangledName = Sema::mangleGenericName(baseName, typeArgs);

    // Get original declaration
    auto declIt = genericTypeDecls_.find(baseName);
    if (declIt == genericTypeDecls_.end())
        return;

    // Build substitution map
    std::map<std::string, TypeRef> substitutions;
    const auto& genericParams = getGenericParams(declIt->second.get());
    for (size_t i = 0; i < genericParams.size() && i < typeArgs.size(); ++i)
    {
        substitutions[genericParams[i]] = typeArgs[i];
    }

    // Create specialized type info
    if (auto* valueDecl = dynamic_cast<ValueDecl*>(declIt->second.get()))
    {
        ValueTypeInfo info;
        info.name = mangledName;
        info.size = 0;

        // Process fields with substitution
        for (const auto& field : valueDecl->fields)
        {
            TypeRef fieldType = substituteTypeParams(
                sema_.resolveTypeNode(field.type.get()), substitutions);

            FieldLayout layout;
            layout.name = field.name;
            layout.type = fieldType;
            layout.offset = info.size;
            layout.size = typeSize(fieldType);

            info.fields.push_back(layout);
            info.size += layout.size;
        }

        // Register specialized type
        valueTypes_[mangledName] = info;

        // Lower specialized methods
        for (const auto& method : valueDecl->methods)
        {
            lowerMethodWithSubstitution(mangledName, method.get(), substitutions);
        }
    }
    // Similar for EntityDecl...
}
```

#### 6.2.3 Lowerer_Expr.cpp - Trigger Instantiation on Use

```cpp
// In lowerNew() when creating generic type instance
LowerResult Lowerer::lowerNew(NewExpr* expr)
{
    TypeRef type = sema_.typeOf(expr);

    // Check if this is a generic type instantiation
    if (type && type->isGeneric() && !type->typeArgs.empty())
    {
        // Request monomorphization
        requestInstantiation(type->name, type->typeArgs);

        // Use mangled name for allocation
        std::string mangledName = Sema::mangleGenericName(type->name, type->typeArgs);

        // Generate allocation call with specialized type
        return lowerNewForType(mangledName, type);
    }

    // ... existing non-generic handling
}

// In lowerCall() when calling generic function
LowerResult Lowerer::lowerCall(CallExpr* expr)
{
    // ... resolve callee ...

    if (isGenericFunction(calleeName))
    {
        std::vector<TypeRef> typeArgs = getTypeArgsForCall(expr);
        requestInstantiation(calleeName, typeArgs);

        std::string mangledName = Sema::mangleGenericName(calleeName, typeArgs);
        return emitCall(mangledName, args);
    }

    // ... existing handling
}
```

#### 6.2.4 IL Name Mangling Convention

```
Base Type:     Box              → Box
Instantiated:  Box[Integer]     → Box$Integer
Nested:        Box[List[T]]     → Box$List$Integer  (when T=Integer)
Multiple:      Map[K, V]        → Map$String$Integer
Methods:       Box[T].get()     → Box$Integer.get
```

### 6.3 Test Cases

```zia
// test_generics_lowering.zia

// Test 1: Value type monomorphization
value Stack[T] {
    List[T] items;

    func init() {
        items = [];
    }

    func push(item: T) {
        items.add(item);
    }

    func pop() -> T {
        var last = items.get(items.Count - 1);
        items.remove(items.Count - 1);
        return last;
    }

    func isEmpty() -> Boolean {
        return items.Count == 0;
    }
}

// These should generate separate monomorphized versions
var intStack = Stack[Integer]();
intStack.push(1);
intStack.push(2);
var top = intStack.pop();  // Should be 2

var strStack = Stack[String]();
strStack.push("a");
strStack.push("b");
var topStr = strStack.pop();  // Should be "b"

if (top == 2 && topStr == "b") {
    Viper.Terminal.Say("RESULT: ok");
}

// Test 2: Verify IL names (via --emit-il)
// Should see: Stack$Integer.init, Stack$Integer.push, Stack$Integer.pop
// And:        Stack$String.init, Stack$String.push, Stack$String.pop
```

---

## 7. Phase 5: Type Inference Enhancement

### 7.1 Objective
Improve type inference for generic contexts including:
- Constructor inference (`var box = Box(42)` → `Box[Integer]`)
- Return type inference
- Bidirectional inference from expected type

### 7.2 Changes

#### 7.2.1 Constructor Type Inference

```cpp
// In Sema_Expr.cpp, analyzeCall() for constructors
TypeRef Sema::analyzeConstructorCall(CallExpr* expr, TypeRef expectedType)
{
    auto* ident = dynamic_cast<IdentExpr*>(expr->callee.get());
    if (!ident) return types::unknown();

    // Check if this is a generic type constructor
    auto genericIt = genericTypeDecls_.find(ident->name);
    if (genericIt == genericTypeDecls_.end())
        return analyzeNonGenericConstructor(expr);

    // Try to infer type arguments
    std::vector<TypeRef> typeArgs;

    // 1. Check explicit type arguments: Box[Integer](42)
    if (expr->typeArgs && !expr->typeArgs->args.empty())
    {
        for (const auto& arg : expr->typeArgs->args)
        {
            typeArgs.push_back(resolveTypeNode(arg.get()));
        }
    }
    // 2. Infer from expected type: var box: Box[Integer] = Box(42)
    else if (expectedType && expectedType->isGeneric() &&
             expectedType->name == ident->name)
    {
        typeArgs = expectedType->typeArgs;
    }
    // 3. Infer from constructor arguments
    else
    {
        typeArgs = inferConstructorTypeArgs(genericIt->second.get(), expr->args);
    }

    if (typeArgs.empty())
    {
        error(expr->loc, "Cannot infer type arguments for " + ident->name);
        return types::unknown();
    }

    return instantiateGenericType(ident->name, typeArgs);
}
```

#### 7.2.2 Bidirectional Inference

```cpp
// Propagate expected type through expressions
TypeRef Sema::analyzeExprWithExpectedType(Expr* expr, TypeRef expectedType)
{
    // Store expected type for inference
    expectedTypes_[expr] = expectedType;

    TypeRef actualType = analyzeExpr(expr);

    // For generic expressions, try to refine with expected type
    if (expectedType && actualType->kind == TypeKindSem::TypeParam)
    {
        // TypeParam can be refined to expected type
        return expectedType;
    }

    return actualType;
}

// In analyzeVarDecl()
void Sema::analyzeVarDecl(VarStmt* stmt)
{
    TypeRef declaredType = stmt->type ? resolveTypeNode(stmt->type.get()) : nullptr;

    // Pass declared type as expected type for initializer
    TypeRef initType = analyzeExprWithExpectedType(stmt->init.get(), declaredType);

    // If no declared type, use inferred type
    TypeRef varType = declaredType ? declaredType : initType;

    defineVariable(stmt->name, varType);
}
```

### 7.3 Test Cases

```zia
// test_generics_inference.zia

// Test 1: Constructor inference from arguments
var box1 = Box(42);           // Box[Integer]
var box2 = Box("hello");      // Box[String]
var box3 = Box([1, 2, 3]);    // Box[List[Integer]]

// Test 2: Inference from declared type
var box4: Box[Number] = Box(42);  // Integer promoted to Number

// Test 3: Return type inference
func makeBox[T](value: T) -> Box[T] {
    return Box(value);  // Type inferred from return type context
}

var box5 = makeBox(3.14);  // Box[Number]

// Test 4: Inference through chained calls
var result = Box(42).get();  // Integer

// Test 5: Nested inference
var nested = Box(Box(42));   // Box[Box[Integer]]

if (box1.get() == 42 && box2.get() == "hello") {
    Viper.Terminal.Say("RESULT: ok");
}
```

---

## 8. File Change Summary

### 8.1 Modified Files

| File | Changes |
|------|---------|
| `src/frontends/zia/Sema.hpp` | Add type param stack, generic registries, instantiation methods |
| `src/frontends/zia/Sema.cpp` | Implement substitution, instantiation, inference |
| `src/frontends/zia/Sema_Decl.cpp` | Register generic types/functions, defer analysis |
| `src/frontends/zia/Sema_Expr.cpp` | Generic call resolution, constructor inference |
| `src/frontends/zia/Types.hpp` | Add utility methods for generic types |
| `src/frontends/zia/Types.cpp` | Implement type substitution helpers |
| `src/frontends/zia/Lowerer.hpp` | Add monomorphization tracking |
| `src/frontends/zia/Lowerer.cpp` | Implement instantiation queue processing |
| `src/frontends/zia/Lowerer_Decl.cpp` | Lower specialized type/function declarations |
| `src/frontends/zia/Lowerer_Expr.cpp` | Trigger instantiation on use |
| `src/frontends/zia/Lowerer_Expr_Call.cpp` | Handle generic function calls |

### 8.2 New Files

| File | Purpose |
|------|---------|
| `src/tests/zia/test_zia_generics.cpp` | Comprehensive generics test suite |
| `tests/zia_audit/test_generics_*.zia` | Individual test files |

### 8.3 Estimated Line Changes

| Phase | New Lines | Modified Lines | Total |
|-------|-----------|----------------|-------|
| Phase 1 | 150 | 50 | 200 |
| Phase 2 | 300 | 100 | 400 |
| Phase 3 | 250 | 100 | 350 |
| Phase 4 | 450 | 150 | 600 |
| Phase 5 | 200 | 50 | 250 |
| **Total** | **1350** | **450** | **~1800** |

---

## 9. Testing Strategy

### 9.1 Unit Tests

```cpp
// test_zia_generics.cpp

TEST_CASE("Generic type parameter substitution", "[generics]") {
    // Test TypeParam resolution with active substitutions
}

TEST_CASE("Generic type instantiation", "[generics]") {
    // Test Box[Integer] creates distinct type from Box[String]
}

TEST_CASE("Generic function inference", "[generics]") {
    // Test type argument inference from call arguments
}

TEST_CASE("Monomorphization generates correct IL", "[generics]") {
    // Verify IL contains specialized functions
}
```

### 9.2 Integration Tests

```zia
// Comprehensive end-to-end tests
module TestGenericsComprehensive;

// All generic features working together
value Result[T, E] {
    Boolean isOk;
    T value;
    E error;

    func ok(v: T) -> Result[T, E] { ... }
    func err(e: E) -> Result[T, E] { ... }
    func map[U](f: func(T) -> U) -> Result[U, E] { ... }
}

func divide(a: Number, b: Number) -> Result[Number, String] {
    if (b == 0) {
        return Result[Number, String].err("Division by zero");
    }
    return Result[Number, String].ok(a / b);
}

func start() {
    var result = divide(10, 2)
        .map[String](func(n: Number) -> String {
            return Viper.String.FromFloat(n);
        });

    if (result.isOk && result.value == "5") {
        Viper.Terminal.Say("RESULT: ok");
    }
}
```

### 9.3 Golden Tests

Add IL output verification for:
- Mangled function names
- Specialized struct layouts
- Method table entries

---

## 10. Future Considerations (v0.2+)

### 10.1 Generic Constraints (v0.2)

```zia
// Syntax extension
interface Comparable[T] {
    func compare(other: T) -> Integer;
}

func max[T: Comparable[T]](a: T, b: T) -> T {
    return a.compare(b) > 0 ? a : b;
}
```

**Implementation requirements:**
- AST: Add `constraints` field to generic parameter declarations
- Parser: Extend `parseGenericParams()` for `: Interface` syntax
- Sema: Validate constraints during instantiation
- Lowerer: Generate constraint-aware method dispatch

### 10.2 Variance Annotations (v0.2)

```zia
// Covariance: List[+T] - can use List[Dog] where List[Animal] expected
// Contravariance: Consumer[-T] - can use Consumer[Animal] where Consumer[Dog] expected

interface Producer[+T] {
    func produce() -> T;
}

interface Consumer[-T] {
    func consume(item: T);
}
```

### 10.3 Higher-Kinded Types (v0.3+)

```zia
// Type constructors as parameters
interface Functor[F[_]] {
    func map[A, B](fa: F[A], f: func(A) -> B) -> F[B];
}
```

---

## 11. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Monomorphization explosion | Medium | High | Implement lazy instantiation; add depth limits |
| Type inference ambiguity | Medium | Medium | Clear error messages; explicit syntax fallback |
| Performance regression | Low | Medium | Benchmark before/after; optimize hot paths |
| Breaking existing code | Low | High | Comprehensive test coverage; gradual rollout |
| Complex error messages | High | Medium | Invest in diagnostic quality |

---

## 12. Success Criteria

1. **All existing tests pass** - No regressions
2. **New generic tests pass** - Comprehensive coverage
3. **Performance acceptable** - Compile time < 2x for generic-heavy code
4. **Clear error messages** - Type errors are understandable
5. **Documentation complete** - Zia guide updated with generics

---

## Appendix A: Example IL Output

```
; Source: value Box[T] { T contents; func get() -> T { return contents; } }
; Instantiation: Box[Integer]

; Mangled type definition
struct Box$Integer {
    i64 contents    ; offset 0, size 8
}

; Mangled constructor
func @Box$Integer.init(i64 %value) -> ptr {
entry:
    %self = alloca 8
    %gep = gep ptr, %self, 0
    store i64, %gep, %value
    ret %self
}

; Mangled method
func @Box$Integer.get(ptr %self) -> i64 {
entry:
    %gep = gep ptr, %self, 0
    %val = load i64, %gep
    ret %val
}
```

---

## Appendix B: Grammar Extensions

```ebnf
(* Existing - already implemented in parser *)
generic_params = "[" IDENT ("," IDENT)* "]" ;

(* Type with generic arguments *)
type = base_type generic_args? "?"? ;
generic_args = "[" type ("," type)* "]" ;

(* v0.2: Constraints *)
generic_params_v2 = "[" generic_param ("," generic_param)* "]" ;
generic_param = IDENT (":" type_constraint)? ;
type_constraint = IDENT ("+" IDENT)* ;  (* Interface bounds *)
```
