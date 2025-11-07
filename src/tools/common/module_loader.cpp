//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/module_loader.cpp
// Purpose: Standardise how command-line tools load, verify, and report on IL modules.
// Key invariants: Load results precisely encode whether the failure came from
//                 the file system, the parser, or later verification.  When a
//                 diagnostic is present it always accompanies a parse failure
//                 entry.
// Ownership/Lifetime: The helpers operate on caller-owned modules, streams, and
//                     diagnostic sinks.  Temporary diagnostics are copied into
//                     @ref LoadResult so the caller can persist them beyond the
//                     function call.
// Links: src/tools/common/module_loader.hpp, docs/codemap.md#tools
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides IL module loading and verification helpers for CLI tools.
/// @details Command-line utilities link this implementation to standardise how
///          modules are parsed, validated, and reported to users.

#include "tools/common/module_loader.hpp"

#include "il/api/expected_api.hpp"

#include <fstream>

namespace il::tools::common
{
namespace
{
/// @brief Build a successful load result with no diagnostics attached.
///
/// @details Initialises the @ref LoadResult structure with
///          @ref LoadStatus::Success and leaves the diagnostic slot empty.  The
///          helper keeps the success path uniform so callers can rely on
///          @ref LoadResult::succeeded without worrying about stale diagnostic
///          payloads lingering from earlier attempts.
///
/// @return A result with @ref LoadStatus::Success and an empty diagnostic.
LoadResult makeSuccess()
{
    return {LoadStatus::Success, std::nullopt};
}

/// @brief Create a load result describing an I/O failure.
///
/// @details File system failures (missing file, permission error, etc.) cannot
///          be expressed as IL diagnostics, so the helper records the failure
///          kind while leaving the diagnostic slot empty.  Callers can emit
///          human-readable I/O messages and still propagate the structured
///          status to higher layers.
///
/// @return Result tagged with @ref LoadStatus::FileError and no diagnostic.
LoadResult makeFileError()
{
    return {LoadStatus::FileError, std::nullopt};
}

/// @brief Create a load result populated with a parser diagnostic.
///
/// @details Copies @p diag into the result so the caller retains access to the
///          structured error even after the temporary parser object has been
///          destroyed.  The copy is inexpensive because diagnostics contain
///          small, reference-counted payloads.
///
/// @param diag Diagnostic emitted by the parser that explains the failure.
/// @return A result tagged with @ref LoadStatus::ParseError holding @p diag.
LoadResult makeParseError(const il::support::Diag &diag)
{
    return {LoadStatus::ParseError, diag};
}
} // namespace

/// @brief Load and optionally verify a textual IL module.
///
/// @details The loader performs a full round-trip from disk to in-memory
///          module:
///          1. Open @p path as an input stream and emit @p ioErrorPrefix
///             followed by the failing path when the open call fails.
///          2. Invoke @ref il::api::v2::parse_text_expected to populate
///             @p module, preserving any diagnostics emitted by the parser via
///             the Expected interface.
///          3. On parse failure, forward the diagnostic to @p err through
///             @ref il::support::printDiag and return a parse-error
///             @ref LoadResult constructed by @ref makeParseError.
///          4. On success, return @ref makeSuccess so callers can optionally run
///             verification as a separate step.
///          The helper intentionally leaves verification to the caller because
///          some tools wish to parse without enforcing full structural
///          correctness.
///
/// @param path Filesystem path to the IL file.
/// @param module Module instance receiving parsed content on success.
/// @param err Stream for human-readable diagnostics.
/// @param ioErrorPrefix Prefix emitted before file-system error messages.
/// @return Load status describing success, file errors, or parse failures.
LoadResult loadModuleFromFile(const std::string &path,
                              il::core::Module &module,
                              std::ostream &err,
                              std::string_view ioErrorPrefix)
{
    std::ifstream input(path);
    if (!input)
    {
        err << ioErrorPrefix << path << '\n';
        return makeFileError();
    }

    auto parsed = il::api::v2::parse_text_expected(input, module);
    if (!parsed)
    {
        const auto diag = parsed.error();
        il::support::printDiag(diag, err);
        return makeParseError(diag);
    }

    return makeSuccess();
}

/// @brief Run the IL verifier and forward diagnostics to the caller.
///
/// @details Invokes @ref il::api::v2::verify_module_expected to reuse the
///          Expected-based diagnostic pipeline and prints failures through
///          @ref il::support::printDiag.  A @ref il::support::SourceManager pointer is
///          accepted so tools can render file identifiers as paths when
///          available, closely matching the output produced by the main
///          compiler driver.
///
/// @param module Module that should satisfy verifier invariants.
/// @param err Output stream receiving verifier diagnostics.
/// @param sm Optional source manager providing path lookups for diagnostics.
/// @return @c true when verification succeeds, otherwise @c false after printing
///         diagnostics to @p err.
bool verifyModule(const il::core::Module &module,
                  std::ostream &err,
                  const il::support::SourceManager *sm)
{
    auto verified = il::api::v2::verify_module_expected(module);
    if (!verified)
    {
        il::support::printDiag(verified.error(), err, sm);
        return false;
    }
    return true;
}

} // namespace il::tools::common
