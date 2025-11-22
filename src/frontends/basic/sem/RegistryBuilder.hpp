// File: src/frontends/basic/sem/RegistryBuilder.hpp
// Purpose: Build NamespaceRegistry from parsed BASIC program.
// Key invariants:
//   - Registers all declared namespaces, classes, and interfaces.
//   - Records USING directives in UsingContext.
//   - Case-insensitive registration with canonical spelling preservation.
// Ownership/Lifetime: Does not own registry or usings; caller ensures lifetime.
// Links: docs/codemap.md, CLAUDE.md

#pragma once

#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "frontends/basic/sem/UsingContext.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

namespace il::frontends::basic
{

// Forward declarations
struct Program;
class DiagnosticEmitter;

/// @brief Populate NamespaceRegistry and UsingContext from a parsed BASIC program.
/// @details Clears any pre-existing entries then walks the top-level statements
///          collecting namespace declarations, class declarations, interface declarations,
///          and USING directives. For each namespace the helper records the name; for
///          each class/interface it registers the type in the appropriate namespace.
/// @param program Parsed BASIC program supplying declarations.
/// @param registry Registry instance that receives namespace and type metadata.
/// @param usings Context that receives USING directives.
/// @param emitter Optional diagnostics interface for future checks.
void buildNamespaceRegistry(const Program &program,
                            NamespaceRegistry &registry,
                            UsingContext &usings,
                            DiagnosticEmitter *emitter);

/// @brief Seed runtime class-driven registries in one place.
/// @details Populates TypeRegistry, RuntimePropertyIndex, RuntimeMethodIndex,
///          and seeds NamespaceRegistry with class name prefixes.
void seedRuntimeClassCatalogs(NamespaceRegistry &registry);

} // namespace il::frontends::basic
