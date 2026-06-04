//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the textual serializer for IL modules.  The serializer prints
// deterministic output that mirrors the parser grammar so modules can round
// trip through the textual form for diagnostics and tooling.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Renders IL modules, functions, and instructions to textual form.
/// @details Helper routines convert operands, types, and instruction-specific
///          payloads into the canonical syntax accepted by the parser.  The
///          resulting string is used by human-facing tools and golden tests to
///          verify lowering behaviour.

#include "il/io/Serializer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "il/io/StringEscape.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::io {

using namespace il::core;

namespace {

/// @brief Context for serialization, carrying function-scoped metadata.
/// @details Allows value printing to resolve temp IDs to their declared names,
///          ensuring IL round-trips correctly through serialize/parse cycles.
struct SerializeContext {
    /// @brief Preferred textual name for each SSA temp in the current function.
    std::vector<std::string> valueNames;

    /// @brief Names owned by more than one SSA temp and therefore unsafe to print directly.
    std::unordered_set<std::string> ambiguousValueNames;

    /// @brief Look up a name for the given temp ID.
    /// @param id Temp ID to resolve.
    /// @return The declared name when it is unique and cannot collide with `%tN` fallback syntax.
    [[nodiscard]] std::string_view nameForTemp(unsigned id) const {
        if (id >= valueNames.size())
            return {};
        const auto &name = valueNames[id];
        if (name.empty() || !isValidILIdentifier(name))
            return {};
        if (ambiguousValueNames.count(name) != 0)
            return {};
        if (auto explicitId = parseExplicitTempName(name); explicitId && *explicitId != id)
            return {};
        return name;
    }

    /// @brief Return the name that should be printed for a temp definition or declaration.
    /// @param id Temp ID to resolve.
    /// @return A unique parseable name without the leading `%` sigil.
    [[nodiscard]] std::string printableNameForTemp(unsigned id) const {
        if (auto name = nameForTemp(id); !name.empty())
            return std::string(name);
        return "t" + std::to_string(id);
    }
};

using Formatter = void (*)(const Instr &, std::ostream &, const SerializeContext &);

/// @brief Convert an opcode enumerator into an array index.
/// @param op Opcode value to convert.
/// @return Zero-based index suitable for array lookups.
constexpr size_t toIndex(Opcode op) {
    return static_cast<size_t>(op);
}

/// @brief Validate a symbol identifier before printing it into textual IL.
/// @details The IL grammar has no quoted form for function, extern, global, or
///          block identifiers.  Emitting an invalid name would therefore produce
///          text that the parser cannot read back, so serialization fails fast.
/// @param role Human-readable identifier role used in the exception message.
/// @param name Identifier payload without sigil.
/// @return Reference to @p name when it is valid.
/// @throws std::invalid_argument when @p name is empty or malformed.
const std::string &checkedIdentifier(std::string_view role, const std::string &name) {
    if (name.empty() || !isValidILIdentifier(name)) {
        throw std::invalid_argument("IL serializer: invalid " + std::string(role) +
                                    " identifier '" + name + "'");
    }
    return name;
}

/// @brief Format a value operand into the textual representation used by IL.
/// @details For temp values, attempts to resolve the ID to its declared name
///          using the serialize context. Falls back to %tN format when no
///          name is available.
/// @param os    Stream receiving the textual value.
/// @param value Operand to serialise.
/// @param ctx   Serialization context with value name mappings.
void printValue(std::ostream &os, const Value &value, const SerializeContext &ctx) {
    if (value.kind == Value::Kind::ConstStr) {
        os << '"' << encodeEscapedString(value.str) << '"';
        return;
    }
    // For temp values, try to resolve to declared name for correct round-tripping
    if (value.kind == Value::Kind::Temp) {
        auto name = ctx.nameForTemp(value.id);
        if (!name.empty() && name[0] != '%') {
            // Use the declared name (e.g., "X" for parameter)
            os << '%' << name;
            return;
        }
    }
    if (value.kind == Value::Kind::GlobalAddr) {
        os << '@' << checkedIdentifier("global reference", value.str);
        return;
    }
    os << il::core::toString(value);
}

/// @brief Emit a comma-separated list of operands to a stream.
/// @param os Stream receiving the textual operands.
/// @param values Sequence of operands to print.
/// @param ctx Serialization context with value name mappings.
void printValueList(std::ostream &os,
                    const std::vector<Value> &values,
                    const SerializeContext &ctx) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            os << ", ";
        printValue(os, values[i], ctx);
    }
}

/// @brief Serialize standard operand lists for instructions.
/// @param instr Instruction providing operands.
/// @param os Stream receiving the serialized operands.
/// @param ctx Serialization context with value name mappings.
void printDefaultOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    if (instr.operands.empty())
        return;
    os << ' ';
    printValueList(os, instr.operands, ctx);
}

/// @brief Render the optional trap kind source error operand.
/// @param instr Instruction containing the operand.
/// @param os Stream receiving the textual operand.
/// @param ctx Serialization context with value name mappings.
void printTrapKindOperand(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    if (instr.operands.empty())
        return;
    os << ' ';
    printValue(os, instr.operands.front(), ctx);
}

/// @brief Emit operands for trap.from.err instructions.
/// @param instr Instruction containing the type and optional operand.
/// @param os Stream receiving serialized operands.
/// @param ctx Serialization context with value name mappings.
void printTrapFromErrOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty()) {
        os << ' ';
        printValue(os, instr.operands.front(), ctx);
    }
}

/// @brief Emit operand list for call instructions.
/// @param instr Instruction referencing the callee and operands.
/// @param os Stream receiving serialized operands.
/// @param ctx Serialization context with value name mappings.
void printCallOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    os << " @" << checkedIdentifier("callee", instr.callee) << "(";
    printValueList(os, instr.operands, ctx);
    os << ')';
    const bool hasAttrs = instr.CallAttr.nothrow || instr.CallAttr.readonly || instr.CallAttr.pure;
    if (!hasAttrs)
        return;
    os << " [";
    bool first = true;
    auto printAttr = [&](std::string_view name) {
        if (!first)
            os << ", ";
        first = false;
        os << name;
    };
    if (instr.CallAttr.nothrow)
        printAttr("nothrow");
    if (instr.CallAttr.readonly)
        printAttr("readonly");
    if (instr.CallAttr.pure)
        printAttr("pure");
    os << ']';
}

/// @brief Emit a stable attribute list for function definitions.
void printFunctionAttrs(const FunctionAttrs &attrs, std::ostream &os) {
    const bool hasAttrs = attrs.nothrow || attrs.readonly || attrs.pure;
    if (!hasAttrs)
        return;
    os << " [";
    bool first = true;
    auto printAttr = [&](std::string_view name) {
        if (!first)
            os << ", ";
        first = false;
        os << name;
    };
    if (attrs.nothrow)
        printAttr("nothrow");
    if (attrs.readonly)
        printAttr("readonly");
    if (attrs.pure)
        printAttr("pure");
    os << ']';
}

/// @brief Emit operand list for call.indirect instructions.
/// @details Format: call.indirect %fnPtr(%arg1, %arg2, ...)
///          First operand is the function pointer, remaining are arguments.
void printCallIndirectOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    if (instr.operands.empty()) {
        os << " ; missing callee";
        return;
    }
    os << ' ';
    if (instr.hasIndirectSignature) {
        os << '[' << instr.indirectRetType.toString() << '(';
        for (size_t i = 0; i < instr.indirectParamTypes.size(); ++i) {
            if (i)
                os << ", ";
            os << instr.indirectParamTypes[i].toString();
        }
        if (instr.indirectIsVarArg) {
            if (!instr.indirectParamTypes.empty())
                os << ", ";
            os << "...";
        }
        os << ")] ";
    }
    printValue(os, instr.operands[0], ctx);
    os << '(';
    if (instr.operands.size() > 1) {
        for (size_t i = 1; i < instr.operands.size(); ++i) {
            if (i > 1)
                os << ", ";
            printValue(os, instr.operands[i], ctx);
        }
    }
    os << ')';
}

/// @brief Emit optional return operand for ret instructions.
/// @param instr Return instruction to serialise.
/// @param os Stream receiving serialized operand.
/// @param ctx Serialization context with value name mappings.
void printRetOperand(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    if (instr.operands.empty())
        return;
    os << ' ';
    printValue(os, instr.operands[0], ctx);
}

/// @brief Emit operands for load instructions including type annotation.
/// @param instr Instruction providing the operands.
/// @param os Stream receiving serialized operands.
/// @param ctx Serialization context with value name mappings.
void printLoadOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty()) {
        os << ", ";
        printValue(os, instr.operands[0], ctx);
    }
}

/// @brief Emit operands for store instructions including type annotation.
/// @param instr Instruction providing the destination and value.
/// @param os Stream receiving serialized operands.
/// @param ctx Serialization context with value name mappings.
void printStoreOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty()) {
        os << ", ";
        printValue(os, instr.operands[0], ctx);
        if (instr.operands.size() > 1) {
            os << ", ";
            printValue(os, instr.operands[1], ctx);
        }
    }
}

/// @brief Emit the branch target label and arguments at a given index.
/// @param instr Instruction containing the branch metadata.
/// @param index Successor index to print.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printBranchTarget(const Instr &instr,
                       size_t index,
                       std::ostream &os,
                       const SerializeContext &ctx) {
    if (index >= instr.labels.size())
        return;
    os << checkedIdentifier("label", instr.labels[index]);
    if (index < instr.brArgs.size() && !instr.brArgs[index].empty()) {
        os << '(';
        printValueList(os, instr.brArgs[index], ctx);
        os << ')';
    }
}

/// @brief Emit a caret-prefixed branch target for resume instructions.
/// @param instr Instruction containing the branch metadata.
/// @param index Successor index to print.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printCaretBranchTarget(const Instr &instr,
                            size_t index,
                            std::ostream &os,
                            const SerializeContext &ctx) {
    if (index >= instr.labels.size())
        return;
    os << '^' << instr.labels[index];
    if (index < instr.brArgs.size() && !instr.brArgs[index].empty()) {
        os << '(';
        printValueList(os, instr.brArgs[index], ctx);
        os << ')';
    }
}

/// @brief Emit operands for unconditional branch instructions.
/// @param instr Branch instruction to serialise.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printBrOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    if (instr.labels.empty()) {
        os << " ; missing label";
        return;
    }
    os << ' ';
    printBranchTarget(instr, 0, os, ctx);
}

/// @brief Emit operands for conditional branch instructions.
/// @param instr Branch instruction to serialise.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printCBrOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    if (instr.operands.empty()) {
        os << " ; missing label";
        return;
    }

    os << ' ';
    printValue(os, instr.operands[0], ctx);

    if (instr.labels.empty()) {
        os << " ; missing label";
        return;
    }

    os << ", ";
    printBranchTarget(instr, 0, os, ctx);

    if (instr.labels.size() >= 2) {
        os << ", ";
        printBranchTarget(instr, 1, os, ctx);
    } else {
        os << " ; missing label";
    }
}

/// @brief Emit operands for switch.i32 instructions including case table.
/// @param instr Switch instruction providing the scrutinee and cases.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printSwitchI32Operands(const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
    if (instr.operands.empty()) {
        os << " ; missing scrutinee";
        return;
    }
    if (instr.labels.empty()) {
        os << " ; missing label";
        return;
    }

    os << ' ';
    printValue(os, switchScrutinee(instr), ctx);
    os << ", ";
    printCaretBranchTarget(instr, 0, os, ctx);

    const size_t caseCount = switchCaseCount(instr);
    for (size_t idx = 0; idx < caseCount; ++idx) {
        os << ", ";
        printValue(os, switchCaseValue(instr, idx), ctx);
        os << " -> ";
        printCaretBranchTarget(instr, idx + 1, os, ctx);
    }
}

/// @brief Retrieve the formatter function for a given opcode.
/// @param op Opcode to format.
/// @return Function object responsible for serialising operands of @p op.
const Formatter &formatterFor(Opcode op) {
    static const auto formatters = [] {
        std::array<Formatter, kNumOpcodes> table;
        table.fill(&printDefaultOperands);
        table[toIndex(Opcode::Call)] = &printCallOperands;
        table[toIndex(Opcode::CallIndirect)] = &printCallIndirectOperands;
        table[toIndex(Opcode::Ret)] = &printRetOperand;
        table[toIndex(Opcode::Br)] = &printBrOperands;
        table[toIndex(Opcode::CBr)] = &printCBrOperands;
        table[toIndex(Opcode::SwitchI32)] = &printSwitchI32Operands;
        table[toIndex(Opcode::Load)] = &printLoadOperands;
        table[toIndex(Opcode::Store)] = &printStoreOperands;
        table[toIndex(Opcode::TrapKind)] = &printTrapKindOperand;
        table[toIndex(Opcode::TrapFromErr)] = &printTrapFromErrOperands;
        table[toIndex(Opcode::EhPush)] =
            [](const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
                if (!instr.labels.empty()) {
                    os << ' ';
                    printCaretBranchTarget(instr, 0, os, ctx);
                }
            };
        table[toIndex(Opcode::ResumeLabel)] =
            [](const Instr &instr, std::ostream &os, const SerializeContext &ctx) {
                if (!instr.operands.empty()) {
                    os << ' ';
                    printValue(os, instr.operands[0], ctx);
                }
                if (!instr.labels.empty()) {
                    os << ", ";
                    printCaretBranchTarget(instr, 0, os, ctx);
                }
            };
        return table;
    }();
    return formatters[toIndex(op)];
}

/// @brief Emit a single extern declaration following canonical IL syntax.
/// @param e Imported function descriptor to serialise; not owned.
/// @param os Stream that receives the textual representation; not owned.
void printExtern(const Extern &e, std::ostream &os) {
    os << "extern @" << checkedIdentifier("extern", e.name) << "(";
    for (size_t i = 0; i < e.params.size(); ++i) {
        if (i)
            os << ", ";
        os << e.params[i].toString();
    }
    os << ") -> " << e.retType.toString();
    printFunctionAttrs(e.attrs(), os);
    os << "\n";
}

/// @brief Determine the default result type kind for an opcode.
/// @param info Opcode metadata describing result categories.
/// @return Matching kind when the opcode produces a fixed result type; empty optional otherwise.
std::optional<Type::Kind> defaultResultKind(const OpcodeInfo &info) {
    using Kind = Type::Kind;
    switch (info.resultType) {
        case TypeCategory::I1:
            return Kind::I1;
        case TypeCategory::I16:
            return Kind::I16;
        case TypeCategory::I32:
            return Kind::I32;
        case TypeCategory::I64:
            return Kind::I64;
        case TypeCategory::F64:
            return Kind::F64;
        case TypeCategory::Ptr:
            return Kind::Ptr;
        case TypeCategory::Str:
            return Kind::Str;
        case TypeCategory::Error:
            return Kind::Error;
        case TypeCategory::ResumeTok:
            return Kind::ResumeTok;
        default:
            break;
    }
    return std::nullopt;
}

/// @brief Identify whether a basic block models an exception handler entry.
/// @param bb Block to inspect.
/// @return @c true when @p bb begins with `eh.entry` and carries error/resume parameters.
bool isHandlerBlock(const BasicBlock &bb) {
    if (bb.instructions.empty())
        return false;
    if (bb.instructions.front().op != Opcode::EhEntry)
        return false;
    if (bb.params.size() < 2)
        return false;
    if (bb.params[0].type.kind != Type::Kind::Error)
        return false;
    if (bb.params[1].type.kind != Type::Kind::ResumeTok)
        return false;
    return true;
}

/// @brief Emit a single instruction.
/// @param in Instruction to serialize; operands and labels must satisfy
///           opcode-specific invariants.
/// @param os Stream that receives text output; not owned.
/// @param ctx Serialization context with value name mappings.
/// @format Begins with optional `.loc` metadata, then prints result, opcode,
///         and operands according to `Opcode`. Branches emit labels and
///         associated arguments. Values and types use `toString()` helpers.
/// @assumptions `in`'s operand and label vectors are sized appropriately for
///              its opcode (e.g., `Call` has `callee` and operands, `Br`
///              provides at most one label, `CBr` provides two). The function
///              assumes `os` remains valid for the duration of the call.
void printInstr(const Instr &in, std::ostream &os, const SerializeContext &ctx) {
    if (in.loc.isValid())
        os << "  .loc " << in.loc.file_id << ' ' << in.loc.line << ' ' << in.loc.column << "\n";
    os << "  ";
    const auto &info = getOpcodeInfo(in.op);
    if (in.result) {
        os << '%' << ctx.printableNameForTemp(*in.result);
        if (auto def = defaultResultKind(info)) {
            if (in.type.kind != *def)
                os << ':' << in.type.toString();
        } else if (info.resultType == TypeCategory::Dynamic && in.type.kind != Type::Kind::Void) {
            os << ':' << in.type.toString();
        } else if (info.resultType == TypeCategory::InstrType && in.op != Opcode::Load &&
                   in.type.kind != Type::Kind::Void && in.type.kind != Type::Kind::I64) {
            os << ':' << in.type.toString();
        }
        os << " = ";
    }
    os << il::core::toString(in.op);
    const auto &formatter = formatterFor(in.op);
    formatter(in, os, ctx);
    os << "\n";
}

/// @brief Build function-scoped naming metadata for parseable serialization.
/// @details Optimisation passes may leave stale or duplicate diagnostic names in
///          `valueNames` after cloning, promotion, and CFG rewrites.  The textual
///          format has a single function-wide temp namespace, so names that are
///          duplicated or that look like another temp's `%tN` fallback are not
///          safe to print.  This routine chooses each definition's preferred
///          source name when possible and lets `SerializeContext` fall back to
///          `%t<id>` otherwise.
[[nodiscard]] SerializeContext makeSerializeContext(const Function &function) {
    SerializeContext ctx;

    size_t capacity = function.valueNames.size();
    auto touchId = [&](unsigned id) { capacity = std::max(capacity, static_cast<size_t>(id) + 1); };
    for (const auto &param : function.params)
        touchId(param.id);
    for (const auto &block : function.blocks) {
        for (const auto &param : block.params)
            touchId(param.id);
        for (const auto &instr : block.instructions) {
            if (instr.result)
                touchId(*instr.result);
        }
    }

    ctx.valueNames.assign(capacity, {});
    for (size_t id = 0; id < function.valueNames.size(); ++id)
        ctx.valueNames[id] = function.valueNames[id];

    // Parameter declarations are authoritative for parameter ids; valueNames is
    // only a diagnostic side table and may lag behind CFG edits.
    for (const auto &param : function.params) {
        if (param.id < ctx.valueNames.size())
            ctx.valueNames[param.id] = param.name;
    }
    for (const auto &block : function.blocks) {
        for (const auto &param : block.params) {
            if (param.id < ctx.valueNames.size())
                ctx.valueNames[param.id] = param.name;
        }
    }

    std::unordered_map<std::string, unsigned> nameOwners;
    auto recordDefinitionName = [&](unsigned id) {
        if (id >= ctx.valueNames.size())
            return;
        const auto &name = ctx.valueNames[id];
        if (name.empty() || !isValidILIdentifier(name))
            return;
        auto [it, inserted] = nameOwners.emplace(name, id);
        if (!inserted && it->second != id)
            ctx.ambiguousValueNames.insert(name);
    };

    for (const auto &param : function.params)
        recordDefinitionName(param.id);
    for (const auto &block : function.blocks) {
        for (const auto &param : block.params)
            recordDefinitionName(param.id);
        for (const auto &instr : block.instructions) {
            if (instr.result)
                recordDefinitionName(*instr.result);
        }
    }

    return ctx;
}

} // namespace

/// @brief Serialize an IL module into a textual stream.
/// @param m Module to serialize; not owned.
/// @param os Stream that receives output; not owned.
/// @param mode Controls whether externs are emitted canonically or in definition order.
/// Workflow: print the IL version header, emit externs (sorting them when canonical),
/// then globals and functions by walking their basic blocks and delegating instruction
/// formatting to @c printInstr.
/// @returns Nothing; the serialized form is written directly to @p os.
void Serializer::write(const Module &m, std::ostream &os, Mode mode) {
    os << "il " << m.version << "\n";
    if (m.target)
        os << "target \"" << *m.target << "\"\n";
    if (mode == Mode::Canonical) {
        std::vector<Extern> ex(m.externs.begin(), m.externs.end());
        std::sort(
            ex.begin(), ex.end(), [](const Extern &a, const Extern &b) { return a.name < b.name; });
        for (const auto &e : ex)
            printExtern(e, os);
    } else {
        for (const auto &e : m.externs)
            printExtern(e, os);
    }

    for (const auto &g : m.globals) {
        os << "global ";
        if (g.linkage == Linkage::Export)
            os << "export ";
        else if (g.linkage == Linkage::Import)
            os << "import ";
        if (g.isConst || g.type.kind == Type::Kind::Str)
            os << "const ";
        os << g.type.toString() << " @" << checkedIdentifier("global", g.name);
        if (g.type.kind == Type::Kind::Str) {
            os << " = \"" << encodeEscapedString(g.init) << "\"";
        } else if (g.hasInitializer || !g.init.empty()) {
            os << " = " << g.init;
        }
        os << "\n";
    }

    for (const auto &f : m.functions) {
        SerializeContext ctx = makeSerializeContext(f);

        os << "func ";
        if (f.linkage == Linkage::Export)
            os << "export ";
        else if (f.linkage == Linkage::Import)
            os << "import ";
        os << "@" << checkedIdentifier("function", f.name) << "(";
        for (size_t i = 0; i < f.params.size(); ++i) {
            if (i)
                os << ", ";
            os << f.params[i].type.toString() << " %" << ctx.printableNameForTemp(f.params[i].id);
        }
        if (f.isVarArg) {
            if (!f.params.empty())
                os << ", ";
            os << "...";
        }
        os << ") -> " << f.retType.toString();

        // Import-linkage functions have no body (declaration only)
        if (f.linkage == Linkage::Import) {
            printFunctionAttrs(f.attrs(), os);
            os << "\n";
            continue;
        }

        printFunctionAttrs(f.attrs(), os);
        os << " {\n";
        for (const auto &bb : f.blocks) {
            const bool handler = isHandlerBlock(bb);
            if (handler)
                os << "handler ^" << checkedIdentifier("block label", bb.label);
            else
                os << checkedIdentifier("block label", bb.label);
            if (!bb.params.empty()) {
                os << '(';
                for (size_t i = 0; i < bb.params.size(); ++i) {
                    if (i)
                        os << ", ";
                    os << '%' << ctx.printableNameForTemp(bb.params[i].id) << ':';
                    if (handler) {
                        if (bb.params[i].type.kind == Type::Kind::Error)
                            os << "Error";
                        else if (bb.params[i].type.kind == Type::Kind::ResumeTok)
                            os << "ResumeTok";
                        else
                            os << bb.params[i].type.toString();
                    } else {
                        os << bb.params[i].type.toString();
                    }
                }
                os << ')';
            }
            os << ":\n";
            for (const auto &in : bb.instructions)
                printInstr(in, os, ctx);
        }
        os << "}\n";
    }
}

/// @brief Materialize a module's textual IL into an owned string.
/// @param m Module to serialize; not owned.
/// @param mode Printing strategy forwarded to @c write.
/// Workflow: accumulate output in an @c ostringstream by delegating to @c write
/// with the requested @p mode and return the resulting buffer.
/// @returns Canonical or declared-order IL text depending on @p mode.
std::string Serializer::toString(const Module &m, Mode mode) {
    std::ostringstream oss;
    write(m, oss, mode);
    return oss.str();
}

} // namespace il::io
