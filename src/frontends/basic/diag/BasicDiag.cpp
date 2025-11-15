//===----------------------------------------------------------------------===//
// Generated file -- do not edit manually.
//===----------------------------------------------------------------------===//

#include "viper/diag/BasicDiag.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace il::frontends::basic::diag
{
namespace
{
constexpr std::array<BasicDiagInfo, 21> kDiagTable = {
    {{"BASIC_UNKNOWN_VARIABLE",
      "B1001",
      il::support::Severity::Error,
      "unknown variable '{name}'{suggestion}"},
     {"BASIC_UNKNOWN_ARRAY", "B1001", il::support::Severity::Error, "unknown array '{name}'"},
     {"BASIC_NOT_AN_ARRAY",
      "B2001",
      il::support::Severity::Error,
      "variable '{name}' is not an array"},
     {"BASIC_UNKNOWN_PROCEDURE",
      "B1006",
      il::support::Severity::Error,
      "unknown procedure '{name}'"},
     {"BASIC_DUPLICATE_PARAMETER",
      "B1005",
      il::support::Severity::Error,
      "duplicate parameter '{name}'"},
     {"BASIC_ARRAY_PARAM_TYPE",
      "B2004",
      il::support::Severity::Error,
      "array parameter must be i64 or str"},
     {"BASIC_DUPLICATE_PROCEDURE",
      "B1004",
      il::support::Severity::Error,
      "duplicate procedure '{name}'"},
     {"BASIC_UNKNOWN_STATEMENT",
      "B0001",
      il::support::Severity::Error,
      "unknown statement '{token}'; expected keyword or procedure call"},
     {"BASIC_UNEXPECTED_LINE_NUMBER",
      "B0001",
      il::support::Severity::Error,
      "unexpected line number '{token}' before statement"},
     {"BASIC_UNKNOWN_LINE_LABEL", "B1003", il::support::Severity::Error, "unknown line {label}"},
     {"BASIC_IFACE_DUP_METHOD",
      "B2110",
      il::support::Severity::Error,
      "duplicate method '{method}' in interface '{iface}'"},
     {"BASIC_CLASS_MISSES_IFACE_METHOD",
      "B2111",
      il::support::Severity::Error,
      "class '{class}' does not implement interface method '{method}' from '{iface}'"},
     {"NS_UNKNOWN_NAMESPACE",
      "E_NS_001",
      il::support::Severity::Error,
      "namespace not found: '{ns}'"},
     {"NS_TYPE_NOT_IN_NS",
      "E_NS_002",
      il::support::Severity::Error,
      "type '{type}' not found in namespace '{ns}'"},
     {"NS_AMBIGUOUS_TYPE",
      "E_NS_003",
      il::support::Severity::Error,
      "ambiguous reference to '{type}' (found in: {candidates})"},
     {"NS_DUPLICATE_ALIAS",
      "E_NS_004",
      il::support::Severity::Error,
      "duplicate alias: '{alias}' already defined"},
     {"NS_USING_AFTER_DECL",
      "E_NS_005",
      il::support::Severity::Error,
      "USING must appear before namespace or class declarations"},
     {"NS_TYPE_NOT_FOUND",
      "E_NS_006",
      il::support::Severity::Error,
      "cannot resolve type: '{type}'"},
     {"NS_ALIAS_SHADOWS_NS",
      "E_NS_007",
      il::support::Severity::Error,
      "alias '{alias}' conflicts with namespace name"},
     {"NS_USING_NOT_FILE_SCOPE",
      "E_NS_008",
      il::support::Severity::Error,
      "USING cannot appear inside a namespace block"},
     {"NS_RESERVED_VIPER",
      "E_NS_009",
      il::support::Severity::Error,
      "reserved root namespace 'Viper' cannot be declared or imported"}}};
}

const BasicDiagInfo &getInfo(BasicDiag diag)
{
    const auto index = static_cast<std::size_t>(diag);
    return kDiagTable.at(index);
}

std::string_view getId(BasicDiag diag)
{
    return getInfo(diag).id;
}

std::string_view getCode(BasicDiag diag)
{
    return getInfo(diag).code;
}

il::support::Severity getSeverity(BasicDiag diag)
{
    return getInfo(diag).severity;
}

std::string_view getFormat(BasicDiag diag)
{
    return getInfo(diag).format;
}

std::string formatMessage(BasicDiag diag, std::initializer_list<Replacement> replacements)
{
    std::string message(getFormat(diag));
    for (const auto &repl : replacements)
    {
        std::string placeholder;
        placeholder.reserve(repl.key.size() + 2);
        placeholder.push_back('{');
        placeholder.append(repl.key.begin(), repl.key.end());
        placeholder.push_back('}');
        std::size_t pos = 0;
        while ((pos = message.find(placeholder, pos)) != std::string::npos)
        {
            message.replace(pos, placeholder.size(), repl.value);
            pos += repl.value.size();
        }
    }
    return message;
}

} // namespace il::frontends::basic::diag
