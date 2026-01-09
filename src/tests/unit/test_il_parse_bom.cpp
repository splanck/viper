//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_bom.cpp
// Purpose: Verify the IL parser tolerates a UTF-8 BOM on the first line.
// Key invariants: A leading BOM must be stripped before directive matching and
// Ownership/Lifetime: Tests own module instances and in-memory buffers.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <sstream>

int main()
{
    {
        const char src[] = "\xEF\xBB\xBFil 0.2.0\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  ret 0\n"
                           "}\n";
        std::istringstream in(src);
        il::core::Module m;
        std::ostringstream diag;
        auto pe = il::api::v2::parse_text_expected(in, m);
        if (!pe)
        {
            il::support::printDiag(pe.error(), diag);
        }
        assert(pe);
        assert(diag.str().empty());
        assert(m.functions.size() == 1);
        assert(m.functions.front().blocks.size() == 1);
    }

    {
        const char src[] = "    il 0.2.0\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  ret 0\n"
                           "}\n";
        std::istringstream in(src);
        il::core::Module m;
        std::ostringstream diag;
        auto pe = il::api::v2::parse_text_expected(in, m);
        if (!pe)
        {
            il::support::printDiag(pe.error(), diag);
        }
        assert(pe);
        assert(diag.str().empty());
        assert(m.functions.size() == 1);
        assert(m.functions.front().blocks.size() == 1);
    }

    return 0;
}
