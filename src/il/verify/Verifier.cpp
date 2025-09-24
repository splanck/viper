// File: src/il/verify/Verifier.cpp
// Purpose: Coordinates module verification passes for externs, globals, and functions.
// Key invariants: Passes run sequentially and halt on the first structural or typing error.
// Ownership/Lifetime: Operates on caller-owned modules; diagnostic sinks manage their own storage.
// Links: docs/il-guide.md#reference

#include "il/verify/Verifier.hpp"

#include "il/core/Module.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/ExternVerifier.hpp"
#include "il/verify/FunctionVerifier.hpp"
#include "il/verify/GlobalVerifier.hpp"
#include "support/diag_expected.hpp"

#include <sstream>
#include <utility>

using namespace il::core;

namespace il::verify
{
namespace
{
using il::support::Diag;
using il::support::Expected;

Expected<void> appendWarnings(Expected<void> failure, const CollectingDiagSink &sink)
{
    if (sink.diagnostics().empty())
        return failure;

    std::ostringstream oss;
    for (const auto &warning : sink.diagnostics())
        il::support::printDiag(warning, oss);
    il::support::printDiag(failure.error(), oss);

    Diag combined = failure.error();
    combined.message = oss.str();
    return Expected<void>{std::move(combined)};
}

} // namespace

Expected<void> Verifier::verify(const Module &m)
{
    CollectingDiagSink sink;

    ExternVerifier externVerifier;
    if (auto result = externVerifier.run(m, sink); !result)
        return appendWarnings(result, sink);

    GlobalVerifier globalVerifier;
    if (auto result = globalVerifier.run(m, sink); !result)
        return appendWarnings(result, sink);

    FunctionVerifier functionVerifier(externVerifier.externs());
    if (auto result = functionVerifier.run(m, sink); !result)
        return appendWarnings(result, sink);

    return {};
}

} // namespace il::verify
