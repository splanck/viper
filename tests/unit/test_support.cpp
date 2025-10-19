#include "support/arena.hpp"
#include "support/diag_expected.hpp"
#include "support/diagnostics.hpp"
#include "support/result.hpp"
#include "support/source_manager.hpp"
#include "support/string_interner.hpp"
#include <cstdint>
#include <iostream>
#include <cassert>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace il::support
{
struct SourceManagerTestAccess
{
    static void setNextFileId(SourceManager &sm, uint64_t next)
    {
        sm.next_file_id_ = next;
    }
};
} // namespace il::support

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

    // Diagnostics missing a registered path should not emit a leading colon.
    il::support::Diag missingPath{
        il::support::Severity::Error,
        "missing path context",
        il::support::SourceLoc{42, 2, 7},
    };
    std::ostringstream missingDiag;
    il::support::printDiag(missingPath, missingDiag, &sm);
    const std::string missingMessage = missingDiag.str();
    assert(missingMessage.rfind("error: missing path context", 0) == 0);
    assert(!missingMessage.empty() && missingMessage.front() != ':');

    // Expected<Diag> success versus error disambiguation
    std::string diagValueMessage = "value diag";
    il::support::Diag diagValue = il::support::makeError({}, diagValueMessage);
    il::support::Expected<il::support::Diag> ok(il::support::kSuccessDiag,
                                                std::move(diagValue));
    assert(ok.hasValue());
    assert(ok.value().message == diagValueMessage);

    std::string diagErrorMessage = "error diag";
    il::support::Diag diagError = il::support::makeError({}, diagErrorMessage);
    il::support::Expected<il::support::Diag> err(diagError);
    assert(!err.hasValue());
    assert(err.error().message == diagErrorMessage);

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

    // String interner overflow handling
    il::support::StringInterner boundedInterner(2);
    auto s0 = boundedInterner.intern("s0");
    auto s1 = boundedInterner.intern("s1");
    assert(s0);
    assert(s1);
    auto overflow = boundedInterner.intern("s2");
    assert(!overflow);
    assert(boundedInterner.lookup(overflow).empty());
    assert(boundedInterner.intern("s0") == s0);

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

    // SourceManager overflow handling
    {
        il::support::SourceManager overflowSm;
        std::stringstream captured;
        auto *old = std::cerr.rdbuf(captured.rdbuf());
        il::support::SourceManagerTestAccess::setNextFileId(
            overflowSm,
            static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
        uint32_t overflowId = overflowSm.addFile("overflow");
        std::cerr.rdbuf(old);
        assert(overflowId == 0);
        auto diagText = captured.str();
        assert(diagText.find("error:") != std::string::npos);
        assert(diagText.find("source manager exhausted file identifier space")
               != std::string::npos);
    }

    return 0;
}
