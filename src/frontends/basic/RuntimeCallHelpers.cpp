//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/RuntimeCallHelpers.cpp
// Purpose: Implementation of unified runtime call builder for BASIC lowering.
// Key invariants: Builder borrows the Lowerer context and only emits instructions
//                 when a procedure context is active.
// Ownership/Lifetime: Non-owning reference to Lowerer; IR objects remain owned
//                     by the lowering pipeline.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "RuntimeCallHelpers.hpp"
#include "Lowerer.hpp"

namespace il::frontends::basic
{

RuntimeCallBuilder::RuntimeCallBuilder(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

RuntimeCallBuilder &RuntimeCallBuilder::at(il::support::SourceLoc loc) noexcept
{
    loc_ = loc;
    return *this;
}

RuntimeCallBuilder &RuntimeCallBuilder::arg(Value v)
{
    args_.push_back(v);
    return *this;
}

RuntimeCallBuilder &RuntimeCallBuilder::argNarrow32(Value v)
{
    applyLoc();
    Value narrowed = lowerer_.narrow32(v, loc_.value_or(il::support::SourceLoc{}));
    args_.push_back(narrowed);
    return *this;
}

RuntimeCallBuilder &RuntimeCallBuilder::argChannel(Value v, Type ty)
{
    applyLoc();
    Lowerer::RVal channel{v, ty};
    channel = lowerer_.normalizeChannelToI32(std::move(channel), loc_.value_or(il::support::SourceLoc{}));
    args_.push_back(channel.value);
    return *this;
}

RuntimeCallBuilder &RuntimeCallBuilder::argI64(Value v, Type ty)
{
    applyLoc();
    Lowerer::RVal rval{v, ty};
    rval = lowerer_.ensureI64(std::move(rval), loc_.value_or(il::support::SourceLoc{}));
    args_.push_back(rval.value);
    return *this;
}

RuntimeCallBuilder &RuntimeCallBuilder::argF64(Value v, Type ty)
{
    applyLoc();
    Lowerer::RVal rval{v, ty};
    rval = lowerer_.ensureF64(std::move(rval), loc_.value_or(il::support::SourceLoc{}));
    args_.push_back(rval.value);
    return *this;
}

RuntimeCallBuilder &RuntimeCallBuilder::withFeature(RuntimeFeature feature)
{
    lowerer_.requestHelper(feature);
    return *this;
}

RuntimeCallBuilder &RuntimeCallBuilder::trackFeature(RuntimeFeature feature)
{
    lowerer_.trackRuntime(feature);
    return *this;
}

RuntimeCallBuilder &RuntimeCallBuilder::withManualHelper(void (Lowerer::*requireFn)())
{
    (lowerer_.*requireFn)();
    return *this;
}

void RuntimeCallBuilder::call(const std::string &callee)
{
    applyLoc();
    lowerer_.emitCall(callee, args_);
}

RuntimeCallBuilder::Value RuntimeCallBuilder::callRet(Type retTy, const std::string &callee)
{
    applyLoc();
    return lowerer_.emitCallRet(retTy, callee, args_);
}

RuntimeCallBuilder::Value RuntimeCallBuilder::callHelper(RuntimeFeature feature,
                                                          const std::string &callee,
                                                          Type retTy)
{
    applyLoc();
    return lowerer_.emitRuntimeHelper(feature, callee, retTy, args_);
}

void RuntimeCallBuilder::callHelperVoid(RuntimeFeature feature, const std::string &callee)
{
    applyLoc();
    lowerer_.emitRuntimeHelper(feature, callee, Type(Type::Kind::Void), args_);
}

void RuntimeCallBuilder::callWithErrCheck(Type retTy,
                                           const std::string &callee,
                                           std::string_view labelStem)
{
    applyLoc();
    Value err = lowerer_.emitCallRet(retTy, callee, args_);
    lowerer_.emitRuntimeErrCheck(
        err,
        loc_.value_or(il::support::SourceLoc{}),
        labelStem,
        [this](Value code) { lowerer_.emitTrapFromErr(code); });
}

void RuntimeCallBuilder::callWithErrHandler(Type retTy,
                                             const std::string &callee,
                                             std::string_view labelStem,
                                             const std::function<void(Value)> &onFailure)
{
    applyLoc();
    Value err = lowerer_.emitCallRet(retTy, callee, args_);
    lowerer_.emitRuntimeErrCheck(err, loc_.value_or(il::support::SourceLoc{}), labelStem, onFailure);
}

const std::vector<RuntimeCallBuilder::Value> &RuntimeCallBuilder::args() const noexcept
{
    return args_;
}

std::optional<il::support::SourceLoc> RuntimeCallBuilder::location() const noexcept
{
    return loc_;
}

void RuntimeCallBuilder::clearArgs() noexcept
{
    args_.clear();
}

void RuntimeCallBuilder::applyLoc() const
{
    if (loc_)
        lowerer_.curLoc = *loc_;
}

} // namespace il::frontends::basic
