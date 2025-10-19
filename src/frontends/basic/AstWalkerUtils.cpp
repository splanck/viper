// File: src/frontends/basic/AstWalkerUtils.cpp
// Purpose: Defines diagnostics helpers shared by BASIC AST walkers.
// Key invariants: Helpers are side-effect free unless tracing is enabled.
// Ownership/Lifetime: Operate on borrowed node metadata only.
// Links: docs/codemap.md

#include "frontends/basic/AstWalkerUtils.hpp"

namespace il::frontends::basic
{
namespace walker_detail
{

void logHookInvocation(WalkerHook /*hook*/, std::string_view /*nodeType*/) noexcept {}

void logChildHookInvocation(WalkerHook /*hook*/,
                            std::string_view /*parentType*/,
                            std::string_view /*childType*/) noexcept {}

} // namespace walker_detail

} // namespace il::frontends::basic

