//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file LowererCallArgumentLowerer.hpp
/// @brief Helper for lowering semantically bound call/new arguments.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/Lowerer.hpp"

namespace il::frontends::zia {

/// @brief Converts source call arguments into final IL call argument vectors.
///
/// @details Handles named-argument ordering, default parameters, and variadic
///          list packing for both function calls and `new` expressions.
class CallArgumentLowerer final {
  public:
    explicit CallArgumentLowerer(Lowerer &lowerer) : lowerer_(lowerer) {}

    std::vector<LowerResult> lowerSourceArgs(const std::vector<CallArg> &args);
    static std::vector<int> orderedArgSources(const std::vector<CallArg> &args,
                                              const Sema::CallArgBinding *binding);
    std::vector<Lowerer::Value> lowerResolvedArgs(const std::vector<CallArg> &args,
                                                  const std::vector<TypeRef> &paramTypes,
                                                  const std::vector<Param> *params,
                                                  const Sema::CallArgBinding *binding);
    std::vector<Lowerer::Value> lowerResolvedCallArgs(CallExpr *expr,
                                                      const std::vector<TypeRef> &paramTypes,
                                                      const std::vector<Param> *params);
    std::vector<Lowerer::Value> lowerResolvedNewArgs(NewExpr *expr,
                                                     const std::vector<TypeRef> &paramTypes,
                                                     const std::vector<Param> *params);

  private:
    using Type = il::core::Type;
    using Value = il::core::Value;

    Lowerer &lowerer_;
};

} // namespace il::frontends::zia
