// File: tests/e2e/support/FloatOut.h
// Purpose: Declare helper for comparing floating-point outputs in e2e tests.
// Key invariants: Each EXPECT≈ directive corresponds to one output line.
// Ownership/Lifetime: Stateless functions; no global state.
// Links: docs/testing.md
#pragma once

#include <string>

int check_float_output(const std::string &out_file, const std::string &expect_file);
