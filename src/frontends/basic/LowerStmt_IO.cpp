//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/LowerStmt_IO.cpp
// Purpose: Lower BASIC terminal and file I/O statements into IL and runtime
//          helper calls.
// Key invariants: Channel numbers are normalised through the Lowerer helpers
//                 and every runtime invocation performs an accompanying error
//                 check when required by the language semantics.
// Ownership/Lifetime: Functions borrow the @ref Lowerer context and never own
//                     AST nodes or IL modules.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <optional>
#include <string>

namespace
{
constexpr std::size_t kPrintZoneWidth = 14;

std::optional<std::size_t> estimatePrintWidth(const il::frontends::basic::Expr &expr)
{
    using namespace il::frontends::basic;

    if (const auto *stringExpr = dynamic_cast<const StringExpr *>(&expr))
        return stringExpr->value.size();

    if (const auto *intExpr = dynamic_cast<const IntExpr *>(&expr))
        return std::to_string(intExpr->value).size();

    if (const auto *floatExpr = dynamic_cast<const FloatExpr *>(&expr))
    {
        char buffer[64];
        int written = std::snprintf(buffer, sizeof(buffer), "%.15g", floatExpr->value);
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(buffer))
            return std::nullopt;
        return static_cast<std::size_t>(written);
    }

    if (const auto *boolExpr = dynamic_cast<const BoolExpr *>(&expr))
        return boolExpr->value ? std::size_t{2} : std::size_t{1};

    return std::nullopt;
}
} // namespace

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Lower an OPEN statement to the runtime helper sequence.
///
/// @details Evaluates the path and channel expressions, normalises the channel
///          to a 32-bit descriptor, and invokes `rt_open_err_vstr`.  Any runtime
///          error triggers emission of a `trap.from_err` via @ref emitRuntimeErrCheck.
///
/// @param stmt Parsed OPEN statement containing operands and source location.
void Lowerer::lowerOpen(const OpenStmt &stmt)
{
    if (!stmt.pathExpr || !stmt.channelExpr)
        return;

    RVal path = lowerExpr(*stmt.pathExpr);
    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    Value modeValue = emitCommon(stmt.loc).narrow_to(
        Value::constInt(static_cast<int32_t>(stmt.mode)), 64, 32);

    Value err = emitCallRet(
        Type(Type::Kind::I32), "rt_open_err_vstr", {path.value, modeValue, channel.value});

    emitRuntimeErrCheck(err, stmt.loc, "open", [&](Value code) { emitTrapFromErr(code); });
}

/// @brief Lower a CLOSE statement that releases an open channel.
///
/// @details Normalises the channel expression, calls `rt_close_err`, and hooks
///          the result into the standard runtime error check pipeline.
///
/// @param stmt Parsed CLOSE statement.
void Lowerer::lowerClose(const CloseStmt &stmt)
{
    if (!stmt.channelExpr)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    curLoc = stmt.loc;
    Value err = emitCallRet(Type(Type::Kind::I32), "rt_close_err", {channel.value});

    emitRuntimeErrCheck(err, stmt.loc, "close", [&](Value code) { emitTrapFromErr(code); });
}

/// @brief Lower a SEEK statement that repositions a file channel.
///
/// @details Converts the channel to an @c i32 descriptor, coerces the position
///          expression to @c i64, and emits a call to `rt_seek_ch_err` with
///          diagnostic handling.
///
/// @param stmt Parsed SEEK statement.
void Lowerer::lowerSeek(const SeekStmt &stmt)
{
    if (!stmt.channelExpr || !stmt.positionExpr)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    RVal position = lowerExpr(*stmt.positionExpr);
    position = ensureI64(std::move(position), stmt.loc);

    curLoc = stmt.loc;
    Value err =
        emitCallRet(Type(Type::Kind::I32), "rt_seek_ch_err", {channel.value, position.value});

    emitRuntimeErrCheck(err, stmt.loc, "seek", [&](Value code) { emitTrapFromErr(code); });
}

/// @brief Lower a PRINT statement to a series of runtime calls.
///
/// @details Iterates over each print item, lowering expressions to the
///          appropriate runtime helper and emitting spacing semantics for commas
///          and semicolons.  The helper appends a newline when the trailing item
///          is not a semicolon, mirroring BASIC behaviour.
///
/// @param stmt Parsed PRINT statement with expression list.
void Lowerer::lowerPrint(const PrintStmt &stmt)
{
    std::size_t column = 1;
    bool columnKnown = true;

    auto updateColumn = [&](std::optional<std::size_t> width)
    {
        if (!columnKnown)
            return;
        if (width)
            column += *width;
        else
            columnKnown = false;
    };

    auto resetColumn = [&]()
    {
        column = 1;
        columnKnown = true;
    };

    for (const auto &it : stmt.items)
    {
        switch (it.kind)
        {
            case PrintItem::Kind::Expr:
            {
                std::optional<std::size_t> widthEstimate;
                if (it.expr)
                    widthEstimate = estimatePrintWidth(*it.expr);

                RVal value = lowerExpr(*it.expr);
                curLoc = stmt.loc;
                if (value.type.kind == Type::Kind::Str)
                {
                    emitCall("rt_print_str", {value.value});
                    updateColumn(widthEstimate);
                    break;
                }
                if (value.type.kind == Type::Kind::F64)
                {
                    emitCall("rt_print_f64", {value.value});
                    updateColumn(widthEstimate);
                    break;
                }
                value = lowerScalarExpr(std::move(value), stmt.loc);
                emitCall("rt_print_i64", {value.value});
                updateColumn(widthEstimate);
                break;
            }
            case PrintItem::Kind::Comma:
            {
                std::size_t spaces = kPrintZoneWidth;
                if (columnKnown)
                {
                    std::size_t offset = (column - 1) % kPrintZoneWidth;
                    spaces = kPrintZoneWidth - offset;
                    column += spaces;
                }
                std::string padding(spaces, ' ');
                std::string spaceLbl = getStringLabel(padding);
                Value sp = emitConstStr(spaceLbl);
                curLoc = stmt.loc;
                emitCall("rt_print_str", {sp});
                break;
            }
            case PrintItem::Kind::Semicolon:
                break;
        }
    }

    bool suppress_nl = !stmt.items.empty() && stmt.items.back().kind == PrintItem::Kind::Semicolon;
    if (!suppress_nl)
    {
        std::string nlLbl = getStringLabel("\n");
        Value nl = emitConstStr(nlLbl);
        curLoc = stmt.loc;
        emitCall("rt_print_str", {nl});
        resetColumn();
    }
}

/// @brief Convert a PRINT# argument into a runtime string representation.
///
/// @details Determines whether the expression represents a string or numeric
///          value, performs any necessary narrowing to match runtime helper
///          contracts, and optionally quotes string values for CSV emission.
///          The helper returns both the lowered string and the runtime feature
///          that must be requested for linking.
///
/// @param expr AST expression being lowered.
/// @param value Result of lowering the expression.
/// @param quoteStrings Whether string arguments should be quoted for CSV mode.
/// @return Lowered string value and optional runtime feature dependency.
Lowerer::PrintChArgString Lowerer::lowerPrintChArgToString(const Expr &expr,
                                                           RVal value,
                                                           bool quoteStrings)
{
    if (value.type.kind == Type::Kind::Str)
    {
        if (!quoteStrings)
            return {value.value, std::nullopt};

        curLoc = expr.loc;
        Value quoted = emitCallRet(Type(Type::Kind::Str), "rt_csv_quote_alloc", {value.value});
        return {quoted, il::runtime::RuntimeFeature::CsvQuote};
    }

    TypeRules::NumericType numericType = classifyNumericType(expr);
    const char *runtime = nullptr;
    il::runtime::RuntimeFeature feature = il::runtime::RuntimeFeature::StrFromDouble;

    auto narrowInteger = [&](Type::Kind target)
    {
        value = ensureI64(std::move(value), expr.loc);
        int bits = 64;
        switch (target)
        {
            case Type::Kind::I16:
                bits = 16;
                break;
            case Type::Kind::I32:
                bits = 32;
                break;
            case Type::Kind::I1:
                bits = 1;
                break;
            default:
                bits = 64;
                break;
        }
        value.value = emitCommon(expr.loc).narrow_to(value.value, 64, bits);
        value.type = Type(target);
    };

    switch (numericType)
    {
        case TypeRules::NumericType::Integer:
            runtime = "rt_str_i16_alloc";
            feature = il::runtime::RuntimeFeature::StrFromI16;
            narrowInteger(Type::Kind::I16);
            break;
        case TypeRules::NumericType::Long:
            runtime = "rt_str_i32_alloc";
            feature = il::runtime::RuntimeFeature::StrFromI32;
            narrowInteger(Type::Kind::I32);
            break;
        case TypeRules::NumericType::Single:
            runtime = "rt_str_f_alloc";
            feature = il::runtime::RuntimeFeature::StrFromSingle;
            value = ensureF64(std::move(value), expr.loc);
            break;
        case TypeRules::NumericType::Double:
        default:
            runtime = "rt_str_d_alloc";
            feature = il::runtime::RuntimeFeature::StrFromDouble;
            value = ensureF64(std::move(value), expr.loc);
            break;
    }

    curLoc = expr.loc;
    Value text = emitCallRet(Type(Type::Kind::Str), runtime, {value.value});
    return {text, feature};
}

/// @brief Concatenate PRINT# arguments into a comma-delimited record.
///
/// @details Lowers each argument to a string, requests any needed runtime
///          helpers, and concatenates values using the runtime `rt_concat`
///          helper.  When no arguments are present the helper returns an empty
///          string literal handle.
///
/// @param stmt Parsed PRINT# statement describing the arguments.
/// @return Runtime string handle suitable for writing.
Value Lowerer::buildPrintChWriteRecord(const PrintChStmt &stmt)
{
    Value record{};
    bool hasRecord = false;
    std::string commaLbl = getStringLabel(",");
    Value comma = emitConstStr(commaLbl);

    for (const auto &arg : stmt.args)
    {
        if (!arg)
            continue;

        RVal value = lowerExpr(*arg);
        PrintChArgString lowered = lowerPrintChArgToString(*arg, std::move(value), true);
        if (lowered.feature)
            requestHelper(*lowered.feature);

        if (!hasRecord)
        {
            record = lowered.text;
            hasRecord = true;
            continue;
        }

        curLoc = arg->loc;
        record = emitCallRet(Type(Type::Kind::Str), "rt_concat", {record, comma});
        record = emitCallRet(Type(Type::Kind::Str), "rt_concat", {record, lowered.text});
    }

    if (!hasRecord)
    {
        std::string emptyLbl = getStringLabel("");
        record = emitConstStr(emptyLbl);
    }

    return record;
}

/// @brief Lower a PRINT# or WRITE# statement.
///
/// @details Normalises the channel, determines whether the statement is WRITE
///          (which aggregates arguments) or PRINT (which streams them
///          individually), and emits calls to `rt_println_ch_err`.  Each call is
///          wrapped in runtime error checking.
///
/// @param stmt Parsed PRINT#/WRITE# statement.
void Lowerer::lowerPrintCh(const PrintChStmt &stmt)
{
    if (!stmt.channelExpr)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    bool isWrite = stmt.mode == PrintChStmt::Mode::Write;

    if (stmt.args.empty())
    {
        if (stmt.trailingNewline || isWrite)
        {
            std::string emptyLbl = getStringLabel("");
            Value empty = emitConstStr(emptyLbl);
            curLoc = stmt.loc;
            Value err =
                emitCallRet(Type(Type::Kind::I32), "rt_println_ch_err", {channel.value, empty});
            const char *context = isWrite ? "write" : "printch";
            emitRuntimeErrCheck(err, stmt.loc, context, [&](Value code) { emitTrapFromErr(code); });
        }
        return;
    }

    if (isWrite)
    {
        Value record = buildPrintChWriteRecord(stmt);
        curLoc = stmt.loc;
        Value err =
            emitCallRet(Type(Type::Kind::I32), "rt_println_ch_err", {channel.value, record});
        emitRuntimeErrCheck(err, stmt.loc, "write", [&](Value code) { emitTrapFromErr(code); });
        return;
    }

    for (auto it = stmt.args.begin(); it != stmt.args.end(); ++it)
    {
        const auto &arg = *it;
        if (!arg)
            continue;

        RVal value = lowerExpr(*arg);
        PrintChArgString lowered = lowerPrintChArgToString(*arg, std::move(value), false);
        if (lowered.feature)
            requestHelper(*lowered.feature);

        auto nextIt = it;
        do
        {
            ++nextIt;
        } while (nextIt != stmt.args.end() && !*nextIt);

        bool hasNext = nextIt != stmt.args.end();
        const char *helper = "rt_write_ch_err";
        if (stmt.trailingNewline && !hasNext)
            helper = "rt_println_ch_err";

        curLoc = arg->loc;
        Value err = emitCallRet(Type(Type::Kind::I32), helper, {channel.value, lowered.text});
        emitRuntimeErrCheck(err, arg->loc, "printch", [&](Value code) { emitTrapFromErr(code); });
    }

    if (stmt.trailingNewline)
    {
        auto hasPrintedArg = std::any_of(stmt.args.begin(), stmt.args.end(), [](const auto &expr) {
            return static_cast<bool>(expr);
        });
        if (!hasPrintedArg)
        {
            std::string emptyLbl = getStringLabel("");
            Value empty = emitConstStr(emptyLbl);
            curLoc = stmt.loc;
            Value err = emitCallRet(
                Type(Type::Kind::I32), "rt_println_ch_err", {channel.value, empty});
            emitRuntimeErrCheck(err, stmt.loc, "printch", [&](Value code) { emitTrapFromErr(code); });
        }
    }
}

/// @brief Lower an INPUT statement that reads from the console.
///
/// @details Optionally prints the prompt, reads a line from the runtime, splits
///          fields when multiple variables are present, and stores each field
///          into the appropriate slot with type-specific conversions and string
///          release management.
///
/// @param stmt Parsed INPUT statement.
void Lowerer::lowerInput(const InputStmt &stmt)
{
    curLoc = stmt.loc;
    if (stmt.prompt)
    {
        if (auto *se = dynamic_cast<const StringExpr *>(stmt.prompt.get()))
        {
            std::string lbl = getStringLabel(se->value);
            Value v = emitConstStr(lbl);
            emitCall("rt_print_str", {v});
        }
    }
    if (stmt.vars.empty())
        return;

    Value line = emitCallRet(Type(Type::Kind::Str), "rt_input_line", {});

    auto storeField = [&](const std::string &name, Value field)
    {
        SlotType slotInfo = getSlotType(name);
        const auto *info = findSymbol(name);
        assert(info && info->slotId);
        Value target = Value::temp(*info->slotId);
        if (slotInfo.type.kind == Type::Kind::Str)
        {
            curLoc = stmt.loc;
            emitStore(Type(Type::Kind::Str), target, field);
            return;
        }

        if (slotInfo.type.kind == Type::Kind::F64)
        {
            Value f = emitCallRet(Type(Type::Kind::F64), "rt_to_double", {field});
            curLoc = stmt.loc;
            emitStore(Type(Type::Kind::F64), target, f);
            requireStrReleaseMaybe();
            curLoc = stmt.loc;
            emitCall("rt_str_release_maybe", {field});
            return;
        }

        Value n = emitCallRet(Type(Type::Kind::I64), "rt_to_int", {field});
        if (slotInfo.isBoolean)
        {
            Value b = coerceToBool({n, Type(Type::Kind::I64)}, stmt.loc).value;
            curLoc = stmt.loc;
            emitStore(ilBoolTy(), target, b);
        }
        else
        {
            curLoc = stmt.loc;
            emitStore(Type(Type::Kind::I64), target, n);
        }
        requireStrReleaseMaybe();
        curLoc = stmt.loc;
        emitCall("rt_str_release_maybe", {field});
    };

    if (stmt.vars.size() == 1)
    {
        storeField(stmt.vars.front(), line);
        return;
    }

    const long long fieldCount = static_cast<long long>(stmt.vars.size());
    Value fields = emitAlloca(static_cast<int>(fieldCount * 8));
    emitCallRet(
        Type(Type::Kind::I64), "rt_split_fields", {line, fields, Value::constInt(fieldCount)});
    requireStrReleaseMaybe();
    curLoc = stmt.loc;
    emitCall("rt_str_release_maybe", {line});

    for (std::size_t i = 0; i < stmt.vars.size(); ++i)
    {
        long long offset = static_cast<long long>(i * 8);
        Value slot =
            emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), fields, Value::constInt(offset));
        Value field = emitLoad(Type(Type::Kind::Str), slot);
        storeField(stmt.vars[i], field);
    }
}

/// @brief Lower an INPUT# statement for reading a record from a channel.
///
/// @details Allocates temporary slots, performs the channel read via
///          `rt_line_input_ch_err`, splits the result into a single field, and
///          parses it into the target slot according to its declared type.
///
/// @param stmt Parsed INPUT# statement.
void Lowerer::lowerInputCh(const InputChStmt &stmt)
{
    curLoc = stmt.loc;
    Value outSlot = emitAlloca(8);
    emitStore(Type(Type::Kind::Ptr), outSlot, Value::null());

    RVal channel{Value::constInt(stmt.channel), Type(Type::Kind::I64)};
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    Value err =
        emitCallRet(Type(Type::Kind::I32), "rt_line_input_ch_err", {channel.value, outSlot});

    emitRuntimeErrCheck(err, stmt.loc, "lineinputch", [&](Value code) { emitTrapFromErr(code); });

    curLoc = stmt.loc;
    Value line = emitLoad(Type(Type::Kind::Str), outSlot);

    Value fieldSlot = emitAlloca(8);
    emitStore(Type(Type::Kind::Ptr), fieldSlot, Value::null());
    emitCallRet(Type(Type::Kind::I64), "rt_split_fields", {line, fieldSlot, Value::constInt(1)});
    requireStrReleaseMaybe();
    curLoc = stmt.loc;
    emitCall("rt_str_release_maybe", {line});

    curLoc = stmt.loc;
    Value field = emitLoad(Type(Type::Kind::Str), fieldSlot);

    SlotType slotInfo = getSlotType(stmt.target.name);
    const auto *info = findSymbol(stmt.target.name);
    if (!info || !info->slotId)
        return;

    Value slot = Value::temp(*info->slotId);
    if (slotInfo.type.kind == Type::Kind::Str)
    {
        curLoc = stmt.loc;
        emitStore(Type(Type::Kind::Str), slot, field);
        return;
    }

    curLoc = stmt.loc;
    Value fieldCstr = emitCallRet(Type(Type::Kind::Ptr), "rt_string_cstr", {field});

    curLoc = stmt.loc;
    Value parsedSlot = emitAlloca(8);

    if (slotInfo.type.kind == Type::Kind::F64)
    {
        Value err = emitCallRet(Type(Type::Kind::I32), "rt_parse_double", {fieldCstr, parsedSlot});
        emitRuntimeErrCheck(
            err, stmt.loc, "inputch_parse", [&](Value code) { emitTrapFromErr(code); });
        Value parsed = emitLoad(Type(Type::Kind::F64), parsedSlot);
        curLoc = stmt.loc;
        emitStore(Type(Type::Kind::F64), slot, parsed);
    }
    else
    {
        Value err = emitCallRet(Type(Type::Kind::I32), "rt_parse_int64", {fieldCstr, parsedSlot});
        emitRuntimeErrCheck(
            err, stmt.loc, "inputch_parse", [&](Value code) { emitTrapFromErr(code); });
        Value parsed = emitLoad(Type(Type::Kind::I64), parsedSlot);
        if (slotInfo.isBoolean)
        {
            Value b = coerceToBool({parsed, Type(Type::Kind::I64)}, stmt.loc).value;
            curLoc = stmt.loc;
            emitStore(ilBoolTy(), slot, b);
        }
        else
        {
            curLoc = stmt.loc;
            emitStore(Type(Type::Kind::I64), slot, parsed);
        }
    }

    curLoc = stmt.loc;
    emitCall("rt_str_release_maybe", {field});
}

/// @brief Lower a LINE INPUT# statement that reads a full line into a string.
///
/// @details Reads the line through `rt_line_input_ch_err`, stores the result in
///          the target variable when present, and propagates runtime errors.
///
/// @param stmt Parsed LINE INPUT# statement.
void Lowerer::lowerLineInputCh(const LineInputChStmt &stmt)
{
    if (!stmt.channelExpr || !stmt.targetVar)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    curLoc = stmt.loc;
    Value outSlot = emitAlloca(8);
    emitStore(Type(Type::Kind::Ptr), outSlot, Value::null());

    Value err =
        emitCallRet(Type(Type::Kind::I32), "rt_line_input_ch_err", {channel.value, outSlot});

    emitRuntimeErrCheck(err, stmt.loc, "lineinputch", [&](Value code) { emitTrapFromErr(code); });

    curLoc = stmt.loc;
    Value line = emitLoad(Type(Type::Kind::Str), outSlot);

    if (const auto *var = dynamic_cast<const VarExpr *>(stmt.targetVar.get()))
    {
        const auto *info = findSymbol(var->name);
        if (!info || !info->slotId)
            return;
        Value slot = Value::temp(*info->slotId);
        curLoc = stmt.loc;
        emitStore(Type(Type::Kind::Str), slot, line);
    }
}
} // namespace il::frontends::basic
