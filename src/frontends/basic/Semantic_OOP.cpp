//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/Semantic_OOP.cpp
//
// Summary:
//   Public entry point for building the OOP index from a parsed BASIC program.
//   This file provides the buildOopIndex function which is the main API for
//   populating OopIndex with class metadata.
//
//   The implementation is split across multiple translation units:
//   - Semantic_OOP_Helpers.cpp: AST walkers and validation helpers
//   - Semantic_OOP_Builder.cpp: OopIndexBuilder class implementation
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Semantic_OOP.hpp"
#include "frontends/basic/detail/Semantic_OOP_Internal.hpp"

namespace il::frontends::basic
{

//===----------------------------------------------------------------------===//
// Public API
//===----------------------------------------------------------------------===//

void buildOopIndex(const Program &program, OopIndex &index, DiagnosticEmitter *emitter)
{
    detail::OopIndexBuilder builder(index, emitter);
    builder.build(program);
}

} // namespace il::frontends::basic
