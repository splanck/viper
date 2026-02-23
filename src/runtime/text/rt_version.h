//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_version.h
// Purpose: Semantic version parsing and comparison (SemVer 2.0.0) supporting MAJOR.MINOR.PATCH with optional pre-release labels and build metadata.
//
// Key invariants:
//   - Version format: MAJOR.MINOR.PATCH[-pre-release][+build].
//   - Pre-release identifiers are compared lexicographically.
//   - Build metadata is ignored in comparisons.
//   - rt_version_compare returns negative/zero/positive for less/equal/greater.
//
// Ownership/Lifetime:
//   - Version objects are heap-allocated opaque pointers.
//   - Returned strings are newly allocated; caller must release.
//
// Links: src/runtime/text/rt_version.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Parse a semantic version string.
    /// @param str Version string (e.g., "1.2.3", "1.2.3-beta.1+build.42").
    /// @return Opaque version object, or NULL if invalid.
    void *rt_version_parse(rt_string str);

    /// @brief Check if a string is a valid semantic version.
    /// @param str Version string.
    /// @return 1 if valid, 0 otherwise.
    int8_t rt_version_is_valid(rt_string str);

    /// @brief Get the major version number.
    /// @param ver Version object.
    /// @return Major version.
    int64_t rt_version_major(void *ver);

    /// @brief Get the minor version number.
    /// @param ver Version object.
    /// @return Minor version.
    int64_t rt_version_minor(void *ver);

    /// @brief Get the patch version number.
    /// @param ver Version object.
    /// @return Patch version.
    int64_t rt_version_patch(void *ver);

    /// @brief Get the pre-release string.
    /// @param ver Version object.
    /// @return Pre-release string (empty if none).
    rt_string rt_version_prerelease(void *ver);

    /// @brief Get the build metadata string.
    /// @param ver Version object.
    /// @return Build metadata (empty if none).
    rt_string rt_version_build(void *ver);

    /// @brief Format version object back to string.
    /// @param ver Version object.
    /// @return Version string (e.g., "1.2.3-beta.1+build.42").
    rt_string rt_version_to_string(void *ver);

    /// @brief Compare two versions (ignoring build metadata per SemVer spec).
    /// @param a First version object.
    /// @param b Second version object.
    /// @return -1 if a < b, 0 if equal, 1 if a > b.
    int64_t rt_version_cmp(void *a, void *b);

    /// @brief Check if version satisfies a simple constraint.
    /// @param ver Version object.
    /// @param constraint Constraint string (e.g., ">=1.0.0", "<2.0.0", "^1.2", "~1.2.3").
    /// @return 1 if satisfied, 0 otherwise.
    int8_t rt_version_satisfies(void *ver, rt_string constraint);

    /// @brief Bump major version (resets minor and patch to 0).
    /// @param ver Version object.
    /// @return New version string.
    rt_string rt_version_bump_major(void *ver);

    /// @brief Bump minor version (resets patch to 0).
    /// @param ver Version object.
    /// @return New version string.
    rt_string rt_version_bump_minor(void *ver);

    /// @brief Bump patch version.
    /// @param ver Version object.
    /// @return New version string.
    rt_string rt_version_bump_patch(void *ver);

#ifdef __cplusplus
}
#endif
