// File: src/tools/common/module_loader.hpp
// Purpose: Shared helpers for loading and verifying IL modules used by CLI tools.
// Key invariants: LoadResult accurately describes success or failure without mutating the output module on I/O failures.
// Ownership/Lifetime: Callers own provided modules and error streams; helpers borrow them temporarily.
// Links: docs/codemap.md

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
    Success,   ///< Module loaded successfully.
    FileError, ///< Input file could not be opened.
    ParseError ///< Parser reported diagnostics.
};

/// @brief Outcome produced by ::loadModuleFromFile describing the failure mode.
struct LoadResult
{
    LoadStatus status = LoadStatus::Success; ///< High-level status of the load.
    std::optional<il::support::Diag> diag{}; ///< Populated when parsing fails.

    /// @brief Convenience for checking success.
    [[nodiscard]] bool succeeded() const
    {
        return status == LoadStatus::Success;
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

} // namespace il::tools::common

