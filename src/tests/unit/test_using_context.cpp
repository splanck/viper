//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_using_context.cpp
// Purpose: Ensure UsingContext preserves declaration order and handles case-insensitive aliases. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/UsingContext.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

void test_declaration_order_preserved()
{
    UsingContext ctx;

    SourceLoc loc1{1, 1, 1};
    SourceLoc loc2{1, 2, 1};
    SourceLoc loc3{1, 3, 1};

    ctx.add("First.NS", "", loc1);
    ctx.add("Second.NS", "S", loc2);
    ctx.add("Third.NS", "", loc3);

    const auto &imports = ctx.imports();
    assert(imports.size() == 3);

    // Verify declaration order is preserved.
    assert(imports[0].ns == "First.NS");
    assert(imports[0].alias.empty());
    assert(imports[0].loc.line == 1);

    assert(imports[1].ns == "Second.NS");
    assert(imports[1].alias == "S");
    assert(imports[1].loc.line == 2);

    assert(imports[2].ns == "Third.NS");
    assert(imports[2].alias.empty());
    assert(imports[2].loc.line == 3);
}

void test_resolve_alias_case_insensitive()
{
    UsingContext ctx;

    SourceLoc loc{1, 1, 1};
    ctx.add("Foo.Bar.Baz", "FB", loc);

    // All case variations should resolve to the same namespace.
    assert(ctx.resolveAlias("FB") == "Foo.Bar.Baz");
    assert(ctx.resolveAlias("fb") == "Foo.Bar.Baz");
    assert(ctx.resolveAlias("Fb") == "Foo.Bar.Baz");
    assert(ctx.resolveAlias("fB") == "Foo.Bar.Baz");

    // Non-existent alias should return empty string.
    assert(ctx.resolveAlias("Missing").empty());
    assert(ctx.resolveAlias("").empty());
}

void test_has_alias_case_insensitive()
{
    UsingContext ctx;

    SourceLoc loc{1, 1, 1};
    ctx.add("System.IO", "SIO", loc);

    // All case variations should be detected.
    assert(ctx.hasAlias("SIO"));
    assert(ctx.hasAlias("sio"));
    assert(ctx.hasAlias("Sio"));
    assert(ctx.hasAlias("SIo"));

    // Non-existent alias should return false.
    assert(!ctx.hasAlias("Missing"));
    assert(!ctx.hasAlias(""));
}

void test_has_alias_detects_duplicates()
{
    UsingContext ctx;

    SourceLoc loc1{1, 1, 1};
    SourceLoc loc2{1, 2, 1};

    ctx.add("First.NS", "Alias1", loc1);

    // Before adding duplicate, first alias should exist.
    assert(ctx.hasAlias("Alias1"));
    assert(ctx.hasAlias("alias1"));

    // Add another import with the same alias (different case).
    ctx.add("Second.NS", "ALIAS1", loc2);

    // Both should be detectable (but the second will overwrite in alias map).
    assert(ctx.hasAlias("Alias1"));
    assert(ctx.hasAlias("ALIAS1"));

    // The last registration wins for resolveAlias.
    assert(ctx.resolveAlias("alias1") == "Second.NS");
}

void test_clear_removes_all_imports()
{
    UsingContext ctx;

    SourceLoc loc{1, 1, 1};
    ctx.add("NS1", "A1", loc);
    ctx.add("NS2", "A2", loc);
    ctx.add("NS3", "", loc);

    assert(ctx.imports().size() == 3);
    assert(ctx.hasAlias("A1"));
    assert(ctx.hasAlias("A2"));

    ctx.clear();

    assert(ctx.imports().empty());
    assert(!ctx.hasAlias("A1"));
    assert(!ctx.hasAlias("A2"));
    assert(ctx.resolveAlias("A1").empty());
}

void test_empty_alias_no_registration()
{
    UsingContext ctx;

    SourceLoc loc{1, 1, 1};
    ctx.add("Some.Namespace", "", loc);

    // Import should be recorded.
    assert(ctx.imports().size() == 1);
    assert(ctx.imports()[0].ns == "Some.Namespace");
    assert(ctx.imports()[0].alias.empty());

    // No alias should be resolvable.
    assert(!ctx.hasAlias("Some.Namespace"));
    assert(ctx.resolveAlias("Some.Namespace").empty());
}

void test_multiple_imports_same_namespace_different_aliases()
{
    UsingContext ctx;

    SourceLoc loc1{1, 1, 1};
    SourceLoc loc2{1, 2, 1};

    // Same namespace imported twice with different aliases.
    ctx.add("Common.NS", "Alias1", loc1);
    ctx.add("Common.NS", "Alias2", loc2);

    assert(ctx.imports().size() == 2);
    assert(ctx.imports()[0].alias == "Alias1");
    assert(ctx.imports()[1].alias == "Alias2");

    // Both aliases should resolve to the same namespace.
    assert(ctx.resolveAlias("Alias1") == "Common.NS");
    assert(ctx.resolveAlias("Alias2") == "Common.NS");
    assert(ctx.resolveAlias("alias1") == "Common.NS");
    assert(ctx.resolveAlias("ALIAS2") == "Common.NS");
}

void test_mixed_aliased_and_non_aliased_imports()
{
    UsingContext ctx;

    SourceLoc loc{1, 1, 1};
    ctx.add("NS1", "A", loc);
    ctx.add("NS2", "", loc);
    ctx.add("NS3", "B", loc);
    ctx.add("NS4", "", loc);

    const auto &imports = ctx.imports();
    assert(imports.size() == 4);

    assert(imports[0].alias == "A");
    assert(imports[1].alias.empty());
    assert(imports[2].alias == "B");
    assert(imports[3].alias.empty());

    assert(ctx.hasAlias("A"));
    assert(!ctx.hasAlias("NS2"));
    assert(ctx.hasAlias("B"));
    assert(!ctx.hasAlias("NS4"));
}

void test_source_locations_preserved()
{
    UsingContext ctx;

    SourceLoc loc1{10, 5, 8};
    SourceLoc loc2{20, 10, 15};

    ctx.add("NS1", "A1", loc1);
    ctx.add("NS2", "", loc2);

    const auto &imports = ctx.imports();
    assert(imports.size() == 2);

    assert(imports[0].loc.file_id == 10);
    assert(imports[0].loc.line == 5);
    assert(imports[0].loc.column == 8);

    assert(imports[1].loc.file_id == 20);
    assert(imports[1].loc.line == 10);
    assert(imports[1].loc.column == 15);
}

void test_resolve_alias_returns_empty_for_non_existent()
{
    UsingContext ctx;

    SourceLoc loc{1, 1, 1};
    ctx.add("ExistingNS", "ExistingAlias", loc);

    // Existing alias should resolve.
    assert(!ctx.resolveAlias("ExistingAlias").empty());

    // Non-existent aliases should return empty string.
    assert(ctx.resolveAlias("DoesNotExist").empty());
    assert(ctx.resolveAlias("Another").empty());
    assert(ctx.resolveAlias("").empty());
}

int main()
{
    test_declaration_order_preserved();
    test_resolve_alias_case_insensitive();
    test_has_alias_case_insensitive();
    test_has_alias_detects_duplicates();
    test_clear_removes_all_imports();
    test_empty_alias_no_registration();
    test_multiple_imports_same_namespace_different_aliases();
    test_mixed_aliased_and_non_aliased_imports();
    test_source_locations_preserved();
    test_resolve_alias_returns_empty_for_non_existent();
    return 0;
}
