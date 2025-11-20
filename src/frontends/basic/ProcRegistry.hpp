//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ProcRegistry class, which manages BASIC procedure
// (SUB and FUNCTION) signatures and validates procedure declarations and calls.
//
// Procedure Management:
// The ProcRegistry tracks all user-defined procedures in a BASIC program,
// maintaining their signatures for:
// - Forward reference validation: Ensuring calls to procedures declared later
//   in the program are valid
// - Signature checking: Verifying that procedure calls match the declared
//   parameter count and types
// - Duplicate detection: Reporting errors when procedures are defined multiple
//   times with conflicting signatures
//
// Two-Pass Processing:
// The registry supports the semantic analyzer's two-pass approach:
// 1. Declaration pass: Collects all SUB and FUNCTION signatures from the AST
// 2. Validation pass: Checks that all calls match registered signatures
//
// Procedure Signature Information:
// For each procedure, the registry stores:
// - Name: Procedure identifier (case-insensitive in BASIC)
// - Parameters: List of parameter types (Integer, Long, Single, Double, String)
// - Return type: For FUNCTION declarations, the return type; SUB has Void
// - Declaration location: Source location for error reporting
//
// Call Validation:
// When validating a procedure call, the registry checks:
// - The procedure name is defined
// - The argument count matches the parameter count
// - Argument types are compatible with parameter types
// - Functions are called in expression context
// - SUBs are called as statements
//
// Integration:
// - Used by: SemanticAnalyzer during both passes
// - Borrows: SemanticDiagnostics for error reporting
// - No AST ownership: The registry only stores signature metadata
//
// Design Notes:
// - Procedure names are stored in canonical form (uppercase) for
//   case-insensitive lookup
// - Each procedure name maps to exactly one signature; redefinitions are errors
// - The registry does not own AST nodes; it only references declaration metadata
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/runtime/RuntimeSignatures.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "frontends/basic/SemanticDiagnostics.hpp"
#include "frontends/basic/ast/DeclNodes.hpp"

namespace il::frontends::basic
{

struct ProcSignature
{
    enum class Kind
    {
        Function,
        Sub
    } kind{Kind::Function};
    std::optional<Type> retType;

    struct Param
    {
        Type type{Type::I64};
        bool is_array{false};
    };

    std::vector<Param> params;
};

using ProcTable = std::unordered_map<std::string, ProcSignature>;

class ProcRegistry
{
  public:
    explicit ProcRegistry(SemanticDiagnostics &d);

    void clear();

    enum class ProcKind : std::uint8_t
    {
        User,
        BuiltinExtern
    };

    struct ProcEntry
    {
        const void *node{nullptr};
        il::support::SourceLoc loc{};
        ProcKind kind{ProcKind::User};
        // Back-pointer to runtime signature id when BuiltinExtern.
        std::optional<il::runtime::RtSig> runtimeSigId{};
    };

    void registerProc(const FunctionDecl &f);

    void registerProc(const SubDecl &s);

    const ProcTable &procs() const;

    const ProcSignature *lookup(const std::string &name) const;

    // P1.3 API additions
    void AddProc(const FunctionDecl *fn, il::support::SourceLoc loc);
    const ProcEntry *LookupExact(std::string_view qualified) const;

    // Seed registry with builtin extern procedures from runtime signatures.
    void seedRuntimeBuiltins();

  private:
    struct ProcDescriptor
    {
        ProcSignature::Kind kind;
        std::optional<Type> retType;
        std::span<const Param> params;
        il::support::SourceLoc loc;
    };

    ProcSignature buildSignature(const ProcDescriptor &descriptor);

    void registerProcImpl(std::string_view name,
                          const ProcDescriptor &descriptor,
                          il::support::SourceLoc loc);

    SemanticDiagnostics &de;
    ProcTable procs_;
    std::unordered_map<std::string, ProcEntry> byQualified_;
};

} // namespace il::frontends::basic

// RtSig is now available from included header.
