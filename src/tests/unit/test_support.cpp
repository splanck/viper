//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_support.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "support/arena.hpp"
#include "support/diag_expected.hpp"
#include "support/diagnostics.hpp"
#include "support/result.hpp"
#include "support/source_location.hpp"
#include "support/source_manager.hpp"
#include "support/string_interner.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace il::support
{
struct SourceManagerTestAccess
{
    static void setNextFileId(SourceManager &sm, uint64_t next)
    {
        sm.next_file_id_ = next;
    }

    static size_t storedPathCount(const SourceManager &sm)
    {
        return sm.files_.size();
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

    // Cached string_view from lookup must survive further growth.
    il::support::StringInterner stableInterner;
    auto stableSym = stableInterner.intern("stable");
    std::string_view cachedView = stableInterner.lookup(stableSym);
    const char *cachedData = cachedView.data();
    const size_t cachedSize = cachedView.size();
    for (int i = 0; i < 1024; ++i)
    {
        stableInterner.intern("padding_" + std::to_string(i));
        std::string_view probe = stableInterner.lookup(stableSym);
        assert(probe.data() == cachedData);
        assert(probe.size() == cachedSize);
    }
    assert(cachedView.data() == cachedData);
    assert(cachedView.size() == cachedSize);
    assert(cachedView == "stable");

    // Diagnostic formatting
    il::support::SourceManager sm;
    il::support::SourceLoc loc{sm.addFile("test"), 1, 1};
    il::support::DiagnosticEngine de;
    de.report({il::support::Severity::Error, "oops", loc});
    std::ostringstream oss;
    de.printAll(oss, &sm);
    assert(oss.str().find("error: oops") != std::string::npos);
    assert(oss.str().find("test:1:1") != std::string::npos);

    il::support::SourceLoc partial{loc.file_id, 2, 0};
    assert(partial.isValid());
    assert(partial.hasLine());
    assert(!partial.hasColumn());
    il::support::SourceRange mixed{loc, partial};
    assert(mixed.isValid());
    assert(!mixed.end.hasColumn());

    il::support::SourceLoc otherFile{sm.addFile("other"), 3, 5};
    il::support::SourceRange mismatched{loc, otherFile};
    assert(!mismatched.isValid());

    il::support::SourceRange reversed{partial, loc};
    assert(!reversed.isValid());

    il::support::SourceLoc sameLineBegin{loc.file_id, 4, 7};
    il::support::SourceLoc missingColumnEnd{loc.file_id, 4, 0};
    il::support::SourceRange missingColumnRange{sameLineBegin, missingColumnEnd};
    assert(missingColumnRange.isValid());

    il::support::SourceLoc missingLineEnd{loc.file_id, 0, 0};
    il::support::SourceRange missingLineRange{sameLineBegin, missingLineEnd};
    assert(missingLineRange.isValid());
    il::support::Diag partialDiag{il::support::Severity::Error, "partial coordinates", partial};
    std::ostringstream partialStream;
    il::support::printDiag(partialDiag, partialStream, &sm);
    const std::string partialText = partialStream.str();
    assert(partialText.find("test:2: error: partial coordinates") != std::string::npos);
    assert(partialText.find("test:2:0") == std::string::npos);

    // Captured string views must remain valid after subsequent insertions.
    il::support::SourceManager viewSm;
    const uint32_t first_id = viewSm.addFile("first");
    std::string_view first_view = viewSm.getPath(first_id);
    const char *first_data = first_view.data();
    assert(first_view == "first");
    (void)viewSm.addFile("second");
    (void)viewSm.addFile("third");
    std::string_view refreshed_view = viewSm.getPath(first_id);
    assert(first_view == refreshed_view);
    assert(first_data == refreshed_view.data());

    // Re-adding an existing path should reuse the identifier and avoid growth.
    il::support::SourceManager dedupeSm;
    const uint32_t dedupeFirst = dedupeSm.addFile("./dupe/path/../file.txt");
    assert(dedupeFirst != 0);
    const size_t stored_before = il::support::SourceManagerTestAccess::storedPathCount(dedupeSm);
    const uint32_t dedupeSecond = dedupeSm.addFile("dupe/./file.txt");
    assert(dedupeSecond == dedupeFirst);
    [[maybe_unused]] const size_t stored_after =
        il::support::SourceManagerTestAccess::storedPathCount(dedupeSm);
    assert(stored_before == stored_after);
    assert(dedupeSm.getPath(dedupeFirst) == "dupe/file.txt");

#ifdef _WIN32
    // Windows path normalization should ignore ASCII casing to align with
    // case-insensitive filesystem semantics.
    il::support::SourceManager caseInsensitiveSm;
    const uint32_t winFirst = caseInsensitiveSm.addFile("Case/FILE.TXT");
    assert(winFirst != 0);
    const size_t winStoredBefore =
        il::support::SourceManagerTestAccess::storedPathCount(caseInsensitiveSm);
    const uint32_t winSecond = caseInsensitiveSm.addFile("case/file.txt");
    assert(winSecond == winFirst);
    const size_t winStoredAfter =
        il::support::SourceManagerTestAccess::storedPathCount(caseInsensitiveSm);
    assert(winStoredBefore == winStoredAfter);
    assert(caseInsensitiveSm.getPath(winFirst) == "case/file.txt");
#endif

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
    il::support::Expected<il::support::Diag> ok(il::support::kSuccessDiag, std::move(diagValue));
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
    [[maybe_unused]] void *p2 = arena.allocate(sizeof(double), alignof(double));
    assert(reinterpret_cast<uintptr_t>(p2) % alignof(double) == 0);
    const size_t large_align = alignof(std::max_align_t) << 1;
    il::support::Arena large_arena(256);
    [[maybe_unused]] void *p3 = large_arena.allocate(16, large_align);
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

    il::support::Result<int> intError = il::support::Result<int>::error("boom");
    assert(!intError.isOk());
    assert(intError.error() == "boom");

    il::support::Result<std::string> strResult = il::support::Result<std::string>::success("value");
    assert(strResult.isOk());
    assert(strResult.value() == "value");

    il::support::Result<std::string> strError = il::support::Result<std::string>::error("nope");
    assert(!strError.isOk());
    assert(strError.error() == "nope");

    il::support::Result<std::string> literalResult{"ok"};
    assert(literalResult.isOk());
    assert(literalResult.value() == "ok");

    // SourceManager overflow handling
    {
        il::support::SourceManager overflowSm;
        std::stringstream captured;
        auto *old = std::cerr.rdbuf(captured.rdbuf());
        il::support::SourceManagerTestAccess::setNextFileId(
            overflowSm, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
        uint32_t overflowId = overflowSm.addFile("overflow");
        std::cerr.rdbuf(old);
        assert(overflowId == 0);
        auto diagText = captured.str();
        assert(diagText.find("error:") != std::string::npos);
        assert(diagText.find("source manager exhausted file identifier space") !=
               std::string::npos);
    }

    return 0;
}
