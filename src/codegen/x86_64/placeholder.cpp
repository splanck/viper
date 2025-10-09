//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//
// Provides a stubbed entry point for the x86-64 code generation library.  The
// target backend has not yet been implemented, but exporting a real symbol keeps
// the build system and downstream link steps functional while development is in
// progress.
//===----------------------------------------------------------------------===//
namespace il::codegen::x86_64
{

/// @brief Placeholder hook for the unimplemented x86-64 backend.
///
/// The current build needs a concrete symbol to satisfy linkage when the code
/// generation library is produced as part of the toolchain.  The function
/// accepts no arguments, performs no work, and returns zero so that callers can
/// probe library availability without triggering undefined behavior.
///
/// @return Always returns 0 to signal "not yet implemented" without failure.
int placeholder()
{
    return 0;
}
} // namespace il::codegen::x86_64
