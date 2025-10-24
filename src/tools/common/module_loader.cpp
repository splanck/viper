//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements reusable helpers for CLI tools that need to ingest textual IL.
// The routines centralise file I/O, parse diagnostics, and verifier plumbing so
// subcommands can share consistent messaging and error handling. Ownership of
// modules and streams remains with the caller; this layer only orchestrates
// transient parsing and verification work.
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
/// @details This helper keeps the success case consistent across all code paths
///          so the outer control flow can simply check
///          @ref LoadResult::succeeded.
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
///          kind while leaving the diagnostic slot empty.  Callers can then emit
///          bespoke file-system messages.
///
/// @return Result tagged with @ref LoadStatus::FileError and no diagnostic.
LoadResult makeFileError()
{
    return {LoadStatus::FileError, std::nullopt};
}

/// @brief Create a load result populated with a parser diagnostic.
///
/// @details The diagnostic is copied so the caller can print it after the
///          function returns.
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
/// @details Steps performed:
///          1. Attempt to open @p path for reading, printing @p ioErrorPrefix
///             when the file cannot be accessed.
///          2. Parse the IL stream into @p module using the Expected-based API
///             so diagnostics propagate naturally.
///          3. Print parse diagnostics via @ref il::support::printDiag when
///             parsing fails, returning a result tagged
///             @ref LoadStatus::ParseError.
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
/// @details The helper calls @ref il::api::v2::verify_module_expected and
///          prints any resulting diagnostics through @ref il::support::printDiag
///          so command-line tools can keep their verification reporting uniform.
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
