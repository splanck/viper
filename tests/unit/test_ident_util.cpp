// File: tests/unit/test_ident_util.cpp
// Purpose: Unit tests for identifier/path canonicalization utilities.

#include "frontends/basic/IdentifierUtil.hpp"
#include <cassert>
#include <string>
#include <vector>

using namespace il::frontends::basic;

int main()
{
    // Canon lowers ASCII and validates
    assert(Canon("FooBar_123") == std::string("foobar_123"));
    assert(Canon("FOO") == std::string("foo"));
    assert(Canon("foo-BAR").empty()); // '-' invalid

    // JoinDots
    std::vector<std::string> segs{"A", "Bb", "c1"};
    assert(JoinDots(segs) == std::string("A.Bb.c1"));

    // CanonJoin
    assert(CanonJoin(segs) == std::string("a.bb.c1"));

    // SplitDots
    auto parts = SplitDots("One.Two.Three");
    assert(parts.size() == 3);
    assert(parts[0] == std::string("One"));
    assert(parts[1] == std::string("Two"));
    assert(parts[2] == std::string("Three"));

    // Split ignores empties
    auto parts2 = SplitDots("A..B.");
    assert(parts2.size() == 2);
    assert(parts2[0] == std::string("A"));
    assert(parts2[1] == std::string("B"));

    return 0;
}

