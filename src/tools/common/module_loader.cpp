// File: src/tools/common/module_loader.cpp
// Purpose: Implement reusable helpers for loading and verifying IL modules in CLI tools.
// License: MIT License. See LICENSE in the project root for full license information.
// Key invariants: Diagnostics are emitted exactly once per failure and mirror existing tool messaging.
// Ownership/Lifetime: Streams and modules are owned by the caller; this file performs transient operations only.
// Links: docs/codemap.md

#include "tools/common/module_loader.hpp"

#include "il/api/expected_api.hpp"

#include <fstream>

namespace il::tools::common
{
namespace
{
/// @brief Create a successful load result without diagnostics.
///
/// @return A LoadResult marked LoadStatus::Success containing no diagnostic,
///         signalling that parsing and verification succeeded.
LoadResult makeSuccess()
{
    return { LoadStatus::Success, std::nullopt };
}

/// @brief Create a load result for file access failures.
///
/// @return A LoadResult marked LoadStatus::FileError with no diagnostic because
///         file system errors do not yield IL diagnostics.
LoadResult makeFileError()
{
    return { LoadStatus::FileError, std::nullopt };
}

/// @brief Create a load result capturing a parse diagnostic.
///
/// @param diag Diagnostic emitted by the parser describing the failure.
/// @return A LoadResult marked LoadStatus::ParseError carrying the diagnostic
///         for callers to surface to users.
LoadResult makeParseError(const il::support::Diag &diag)
{
    return { LoadStatus::ParseError, diag };
}
} // namespace

/// @brief Load an IL module from disk into an existing module object.
///
/// Opens @p path for reading, parses the contents into @p module using the
/// expected-based API, and streams diagnostics to @p err when failures occur.
///
/// @param path The filesystem location of the IL text to parse.
/// @param module Module instance that receives parsed definitions on success.
/// @param err Output stream for human-readable diagnostics.
/// @param ioErrorPrefix Prefix emitted ahead of file-system error messages.
/// @return LoadStatus::Success when parsing succeeds; LoadStatus::FileError
///         when the file cannot be opened; LoadStatus::ParseError when the IL
///         text fails to parse. Parse errors return a diagnostic alongside the
///         status so callers can surface structured feedback.
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

/// @brief Verify IL module invariants and emit diagnostics on failure.
///
/// Invokes the expected-based verifier and prints diagnostics to @p err when
/// structural or semantic checks fail.
///
/// @param module Module that should satisfy verifier invariants.
/// @param err Output stream receiving verifier diagnostics.
/// @return true when verification succeeds, false otherwise. Verification
///         failures emit diagnostics to @p err to aid CLI tools in reporting
///         actionable errors.
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

