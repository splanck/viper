# Constructor Arguments Analysis

## Summary

**Status**: Working correctly. Constructor arguments in `NEW` expressions are properly passed to the `SUB NEW`
constructor.

## Test Result

```
$ viper front basic -run src/tests/e2e/test_ctor_args.bas
Constructor called with: 10, 20
PASS: constructor args work
```

## Code Flow Analysis

### 1. Parser (`src/frontends/basic/Parser_Stmt_OOP.cpp:620-631`)

When parsing `SUB NEW`:

- Line 620: Identifies `SUB NEW` by checking if the sub name equals "NEW"
- Line 622: Creates a `ConstructorDecl` AST node
- Line 631: **`ctor->params = parseParamList()`** - parses constructor parameters
- Body parsing continues until `END SUB`
- Adds constructor to class members

### 2. Semantic Analysis (`src/frontends/basic/Semantic_OOP_Builder.cpp:123-165`)

Constructor parameters are captured in `processConstructorDecl`:

- Line 154: `info.hasConstructor = true` - marks class as having a constructor
- Lines 155-162: Copies each `ctor.params` into `info.ctorParams`
  ```cpp
  for (const auto &param : ctor.params)
  {
      ClassInfo::CtorParam sigParam;
      sigParam.type = param.type;
      sigParam.isArray = param.is_array;
      info.ctorParams.push_back(sigParam);
  }
  ```

### 3. Constructor Emission (`src/frontends/basic/lower/oop/Lower_OOP_Emit.cpp:256-303`)

The constructor function is generated with parameters:

- Line 256: `metadata.paramCount = 1 + ctor.params.size()` - counts parameters (including ME)
- Line 258: First IR param is `{"ME", Type::Ptr}` - the object reference
- Lines 259-263: Each constructor parameter is added to `metadata.irParams`:
  ```cpp
  for (const auto &param : ctor.params)
  {
      Type ilParamTy = param.is_array ? Type::Ptr : type_conv::astToIlType(param.type);
      metadata.irParams.push_back({param.name, ilParamTy});
  }
  ```
- Line 271: Function name is mangled with `mangleClassCtor(qualify(klass.name))`
- Line 272: IL function is created with all parameters
- Line 303: Parameters are initialized in the constructor body

### 4. NEW Expression Lowering (`src/frontends/basic/lower/oop/Lower_OOP_Alloc.cpp:134-197`)

When lowering `NEW Point(10, 20)`:

1. **Object Allocation** (lines 134-150):
    - Retrieves object size and class ID from `classLayouts_`
    - Calls `rt_obj_new_i64` to allocate the object

2. **VTable Initialization** (lines 152-162):
    - Stores vtable pointer at offset 0

3. **Build Constructor Arguments** (lines 163-193):
   ```cpp
   std::vector<Value> ctorArgs;
   ctorArgs.reserve(expr.args.size() + 1);
   ctorArgs.push_back(obj);  // ME parameter first

   // Look up parameter types for coercion
   if (const ClassInfo *ci = oopIndex_.findClass(qname))
   {
       for (const auto &p : ci->ctorParams)
           ctorParamTypes.push_back(p.type);
   }

   // Lower each argument with type coercion
   for (std::size_t i = 0; i < expr.args.size(); ++i)
   {
       RVal lowered = lowerExpr(*arg);
       // BUG-OOP-007 fix: Coerce to parameter type
       if (i < ctorParamTypes.size())
       {
           // Coerce based on expected type...
       }
       ctorArgs.push_back(lowered.value);
   }
   ```

4. **Call Constructor** (line 197):
   ```cpp
   emitCall(mangleClassCtor(expr.className), ctorArgs);
   ```

## Key Data Structures

### ClassInfo::CtorParam (`src/frontends/basic/OopIndex.hpp:92-98`)

```cpp
struct CtorParam
{
    Type type = Type::I64;   // Declared parameter type
    bool isArray = false;    // True when parameter declared with trailing ()
};
```

### ClassInfo (`src/frontends/basic/OopIndex.hpp:78-141`)

- `hasConstructor`: True if class declares a constructor
- `hasSynthCtor`: True when lowering must synthesize a constructor
- `ctorParams`: Vector of constructor parameter signatures

## Difference: hasConstructor vs hasSynthCtor

- `hasConstructor`: Set when the user explicitly declares `SUB NEW`
- `hasSynthCtor`: Set when no user constructor exists but one needs to be synthesized
    - This happens for classes with initializable fields but no explicit constructor
    - See `Lower_OOP_Emit.cpp:780-786` where a synthetic constructor is created

## Conclusion

Constructor arguments flow correctly through all phases:

1. **Parser** captures parameters in `ConstructorDecl.params`
2. **Semantic Analysis** copies to `ClassInfo.ctorParams` for type checking
3. **Constructor Emission** generates IL function with correct parameters
4. **NEW Expression** builds argument list and calls constructor

No changes needed - the implementation is correct.
