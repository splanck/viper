//===----------------------------------------------------------------------===//
// Generated file -- do not edit manually.
//===----------------------------------------------------------------------===//
#pragma once

#include "support/diagnostics.hpp"
#include <initializer_list>
#include <string>
#include <string_view>

namespace il::frontends::basic::diag
{

enum class BasicDiag
{
    UnknownVariable,
    UnknownArray,
    NotAnArray,
    UnknownProcedure,
    DuplicateParameter,
    ArrayParamType,
    DuplicateProcedure,
    UnknownStatement,
    UnexpectedLineNumber,
    UnknownLineLabel,
    IfaceDupMethod,
    ClassMissesIfaceMethod,
    NsUnknownNamespace,
    NsTypeNotInNs,
    NsAmbiguousType,
    NsDuplicateAlias,
    NsUsingAfterDecl,
    NsTypeNotFound,
    NsAliasShadowsNs,
    NsUsingNotFileScope,
    NsReservedViper,
    ReservedRootDecl
};

struct Replacement
{
    std::string_view key;
    std::string_view value;
};

struct BasicDiagInfo
{
    std::string_view id;
    std::string_view code;
    il::support::Severity severity;
    std::string_view format;
};

[[nodiscard]] const BasicDiagInfo &getInfo(BasicDiag diag);
[[nodiscard]] std::string_view getId(BasicDiag diag);
[[nodiscard]] std::string_view getCode(BasicDiag diag);
[[nodiscard]] il::support::Severity getSeverity(BasicDiag diag);
[[nodiscard]] std::string_view getFormat(BasicDiag diag);
[[nodiscard]] std::string formatMessage(BasicDiag diag,
                                        std::initializer_list<Replacement> replacements = {});

} // namespace il::frontends::basic::diag
