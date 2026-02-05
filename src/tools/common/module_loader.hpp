//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/common/module_loader.hpp
// Purpose: Shared helpers for loading and verifying IL modules used by CLI tools.
// Key invariants: LoadResult accurately describes success or failure without mutating the output
// Ownership/Lifetime: Functions take Module by reference and populate it on success.
//                     Callers own the Module and must keep it alive while using returned results.
//                     LoadResult owns its diagnostic data; safe to copy/move.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Module.hpp"
#include "support/diag_expected.hpp"

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace il::tools::common
{

/// @brief Result classifications for attempting to load a module from disk.
enum class LoadStatus
{
    Success,    ///< Module loaded successfully.
    FileError,  ///< Input file could not be opened.
    ParseError, ///< Parser reported diagnostics.
    VerifyError ///< Verifier reported diagnostics.
};

/// @brief Outcome produced by ::loadModuleFromFile describing the failure mode.
struct LoadResult
{
    LoadStatus status = LoadStatus::Success; ///< High-level status of the load.
    std::optional<il::support::Diag> diag{}; ///< Populated when parsing or verification fails.
    std::string path{};                      ///< Path that was loaded (useful for file errors).

    /// @brief Convenience for checking success.
    [[nodiscard]] bool succeeded() const
    {
        return status == LoadStatus::Success;
    }

    /// @brief Check if the failure was due to file I/O.
    [[nodiscard]] bool isFileError() const
    {
        return status == LoadStatus::FileError;
    }

    /// @brief Check if the failure was due to parsing.
    [[nodiscard]] bool isParseError() const
    {
        return status == LoadStatus::ParseError;
    }

    /// @brief Check if the failure was due to verification.
    [[nodiscard]] bool isVerifyError() const
    {
        return status == LoadStatus::VerifyError;
    }

    /// @brief Human-readable description of the status category.
    [[nodiscard]] const char *statusName() const
    {
        switch (status)
        {
            case LoadStatus::Success:
                return "success";
            case LoadStatus::FileError:
                return "file error";
            case LoadStatus::ParseError:
                return "parse error";
            case LoadStatus::VerifyError:
                return "verify error";
        }
        return "unknown";
    }
};

/// @brief Load an IL module from @p path, printing diagnostics to @p err.
///
/// On success the provided module is populated and the returned status equals
/// LoadStatus::Success. When the file cannot be opened, an explanatory message
/// prefixed by @p ioErrorPrefix is written to @p err and LoadStatus::FileError is
/// returned. Parse diagnostics are forwarded to @p err, stored in the result's
/// diag field, and LoadStatus::ParseError is returned.
///
/// @param path Path to the IL text file to parse.
/// @param module Module receiving the parsed contents when successful.
/// @param err Stream receiving human-readable diagnostics.
/// @param ioErrorPrefix Prefix used when reporting file opening failures.
/// @return Structured result describing success or the failure category.
LoadResult loadModuleFromFile(const std::string &path,
                              il::core::Module &module,
                              std::ostream &err,
                              std::string_view ioErrorPrefix = "unable to open ");

/// @brief Verify @p module and forward diagnostics to @p err when verification fails.
/// @param module Module to verify.
/// @param err Stream receiving diagnostics on error.
/// @param sm Optional source manager used to resolve diagnostic file paths.
/// @return True when verification succeeds; false otherwise.
bool verifyModule(const il::core::Module &module,
                  std::ostream &err,
                  const il::support::SourceManager *sm = nullptr);

/// @brief Verify @p module and return the result without printing.
/// @param module Module to verify.
/// @return LoadResult with VerifyError status on failure, Success otherwise.
LoadResult verifyModuleResult(const il::core::Module &module);

/// @brief Load and verify an IL module from @p path in one step.
///
/// Combines loadModuleFromFile and verifyModuleResult for tools that want both
/// parsing and verification with a single result type.
///
/// @param path Path to the IL text file to parse.
/// @param module Module receiving the parsed contents when successful.
/// @param sm Source manager used to resolve diagnostic file paths.
/// @param err Stream receiving human-readable diagnostics.
/// @param ioErrorPrefix Prefix used when reporting file opening failures.
/// @return Structured result describing success or the failure category.
LoadResult loadAndVerifyModule(const std::string &path,
                               il::core::Module &module,
                               const il::support::SourceManager *sm,
                               std::ostream &err,
                               std::string_view ioErrorPrefix = "unable to open ");

/// @brief Print a LoadResult diagnostic to a stream.
/// @param result Result containing the diagnostic to print.
/// @param err Stream receiving the formatted diagnostic.
/// @param sm Optional source manager used to resolve diagnostic file paths.
void printLoadResult(const LoadResult &result,
                     std::ostream &err,
                     const il::support::SourceManager *sm = nullptr);

} // namespace il::tools::common
