#include "support/arena.hpp"
#include "support/diagnostics.hpp"
#include "support/result.hpp"
#include "support/source_manager.hpp"
#include "support/string_interner.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>

int main()
{
    // String interner uniqueness and lookup
    il::support::StringInterner interner;
    auto a = interner.intern("hello");
    auto b = interner.intern("hello");
    assert(a == b);
    assert(interner.lookup(a) == "hello");

    // Diagnostic formatting
    il::support::SourceManager sm;
    il::support::SourceLoc loc{sm.addFile("test"), 1, 1};
    il::support::DiagnosticEngine de;
    de.report({il::support::Severity::Error, "oops", loc});
    std::ostringstream oss;
    de.printAll(oss, &sm);
    assert(oss.str().find("error: oops") != std::string::npos);
    assert(oss.str().find("test:1:1") != std::string::npos);

    // Arena alignment
    il::support::Arena arena(64);
    void *p1 = arena.allocate(1, 1);
    (void)p1;
    void *p2 = arena.allocate(sizeof(double), alignof(double));
    assert(reinterpret_cast<uintptr_t>(p2) % alignof(double) == 0);
    const size_t large_align = alignof(std::max_align_t) << 1;
    il::support::Arena large_arena(256);
    void *p3 = large_arena.allocate(16, large_align);
    assert(p3 != nullptr);
    assert(reinterpret_cast<uintptr_t>(p3) % large_align == 0);
    assert(arena.allocate(1, 0) == nullptr);
    assert(arena.allocate(1, 3) == nullptr);
    arena.reset();
    arena.allocate(32, 1);
    assert(arena.allocate(std::numeric_limits<size_t>::max() - 15, 1) == nullptr);

    // Result<T> basic success/error flows
    il::support::Result<int> intResult(42);
    assert(intResult.isOk());
    assert(intResult.value() == 42);

    il::support::Result<int> intError(std::string("boom"));
    assert(!intError.isOk());
    assert(intError.error() == "boom");

    il::support::Result<std::string> strResult("value", true);
    assert(strResult.isOk());
    assert(strResult.value() == "value");

    il::support::Result<std::string> strError(std::string("nope"));
    assert(!strError.isOk());
    assert(strError.error() == "nope");

    return 0;
}
