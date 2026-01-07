//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: frontends/basic/detail/Semantic_OOP_Internal.hpp
//
// Summary:
//   Internal declarations shared across the Semantic_OOP translation units.
//   This header is NOT part of the public API and should only be included
//   by Semantic_OOP_*.cpp files within the BASIC frontend.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/OopIndex.hpp"

#include "support/diagnostics.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic::detail
{

//===----------------------------------------------------------------------===//
// Helper Validation Functions
//===----------------------------------------------------------------------===//

/// @brief Check for local variables that shadow class fields.
/// @param body Statement body to scan for shadowing locals.
/// @param klass The class declaration providing field context.
/// @param fieldNames Set of field names to check against.
/// @param emitter Diagnostics interface for emitting warnings.
void checkMemberShadowing(const std::vector<StmtPtr> &body,
                          const ClassDecl &klass,
                          const std::unordered_set<std::string> &fieldNames,
                          DiagnosticEmitter *emitter);

/// @brief Check for use of 'ME' in a static context.
/// @param body Statement body to scan for ME references.
/// @param emitter Diagnostics interface for emitting errors.
/// @param errorCode Error code to emit.
/// @param message Error message to emit.
void checkMeInStaticContext(const std::vector<StmtPtr> &body,
                            DiagnosticEmitter *emitter,
                            const char *errorCode,
                            const char *message);

/// @brief Check if a method body must return a value.
/// @param stmts Statement list forming the method body.
/// @return True if the body definitively returns a value.
[[nodiscard]] bool methodBodyMustReturn(const std::vector<StmtPtr> &stmts);

/// @brief Detect VB-style implicit returns via assignment to the function name.
/// @param method Method declaration to analyze.
/// @return True if the method has an implicit return.
[[nodiscard]] bool methodHasImplicitReturn(const MethodDecl &method);

/// @brief Emit a diagnostic for missing return statement in a function.
/// @param klass Class containing the method.
/// @param method Method declaration to check.
/// @param emitter Diagnostics interface for emitting errors.
void emitMissingReturn(const ClassDecl &klass,
                       const MethodDecl &method,
                       DiagnosticEmitter *emitter);

/// @brief Join qualified name segments with dots.
/// @param segs Vector of name segments.
/// @return Dot-separated qualified name.
[[nodiscard]] std::string joinQualified(const std::vector<std::string> &segs);

//===----------------------------------------------------------------------===//
// OOP Index Builder
//===----------------------------------------------------------------------===//

/// @brief Context for building the OOP index, holding shared state across phases.
/// @details This class encapsulates all the logic for scanning the AST,
///          resolving base classes, building vtables, and checking conformance.
class OopIndexBuilder
{
  public:
    OopIndexBuilder(OopIndex &index, DiagnosticEmitter *emitter) : index_(index), emitter_(emitter)
    {
    }

    /// @brief Build the complete OOP index from a parsed program.
    /// @param program The parsed BASIC program.
    void build(const Program &program);

  private:
    OopIndex &index_;
    DiagnosticEmitter *emitter_;

    // Namespace stack for qualified name construction
    std::vector<std::string> nsStack_;

    // Track raw base names by class for later resolution
    std::unordered_map<std::string, std::pair<std::string, il::support::SourceLoc>> rawBases_;

    // USING directive context
    struct UsingContext
    {
        std::unordered_set<std::string> imports;
        std::unordered_map<std::string, std::string> aliases;
    } usingCtx_;

    // Helper methods
    [[nodiscard]] std::string joinNamespace() const;

    // Phase methods
    void scanClasses(const std::vector<StmtPtr> &stmts);
    void scanInterfaces(const std::vector<StmtPtr> &stmts);
    void collectUsingDirectives(const std::vector<StmtPtr> &stmts);
    void resolveBasesAndImplements();
    void detectInheritanceCycles();
    void buildVtables();
    void checkInterfaceConformance();

    // Class member processing
    void processClassDecl(const ClassDecl &classDecl);
    void processPropertyDecl(const PropertyDecl &prop, ClassInfo &info);
    void processConstructorDecl(const ConstructorDecl &ctor,
                                ClassInfo &info,
                                const ClassDecl &classDecl,
                                const std::unordered_set<std::string> &fieldNames);
    void processMethodDecl(const MethodDecl &method,
                           ClassInfo &info,
                           const ClassDecl &classDecl,
                           const std::unordered_set<std::string> &fieldNames);
    void checkFieldMethodCollisions(ClassInfo &info,
                                    const ClassDecl &classDecl,
                                    const std::unordered_set<std::string> &fieldNames);

    // Resolution helpers
    [[nodiscard]] std::string resolveBase(const std::string &classQ, const std::string &raw) const;
    [[nodiscard]] std::string expandAlias(const std::string &q) const;
    [[nodiscard]] std::string resolveInterface(const std::string &classQ,
                                               const std::string &raw) const;
};

} // namespace il::frontends::basic::detail
