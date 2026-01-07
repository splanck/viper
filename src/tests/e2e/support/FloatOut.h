//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/e2e/support/FloatOut.h
// Purpose: Declare helper for comparing floating-point outputs in e2e tests.
// Key invariants: Each EXPECT≈ directive corresponds to one output line.
// Ownership/Lifetime: Stateless functions; no global state.
// Links: docs/testing.md
#pragma once

#include <string>

/// @brief Compare two files containing floating-point outputs line-by-line.
///
/// @details Parses both files, tolerating minor formatting differences as
///          defined by the e2e floating-point comparison policy (e.g., trimmed
///          whitespace, canonical exponent forms). Returns 0 on a successful
///          match and non-zero on the first discrepancy.
///
/// @param out_file    Path to the produced output file.
/// @param expect_file Path to the golden expectations file.
/// @return 0 on success; non-zero on mismatch or I/O error.
int check_float_output(const std::string &out_file, const std::string &expect_file);
