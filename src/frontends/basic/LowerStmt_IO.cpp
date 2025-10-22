//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lowering utilities for BASIC I/O statements.  The helpers cover
// OPEN/CLOSE/SEEK, PRINT/WRITE, INPUT, and the channel-oriented variants.  Each
// routine translates high-level AST nodes into IL while coordinating runtime
// helper selection, temporary allocation, and structured error reporting.  The
// file lives out-of-line so the header can remain lightweight while the
// implementation centralises the nuanced runtime plumbing and diagnostic
// messaging shared across statements.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Lower an OPEN statement into runtime helper calls.
///
/// @details The method validates that both path and channel expressions are
///          present before lowering them to IL values.  The channel is coerced
///          to a signed 32-bit integer, the mode enum is narrowed to match the
///          runtime ABI, and the `rt_open_err_vstr` helper is invoked.  Any
///          resulting error code is routed through @ref emitRuntimeErrCheck so
///          traps present consistent diagnostics.
///
/// @param stmt AST node describing the BASIC OPEN statement.
void Lowerer::lowerOpen(const OpenStmt &stmt)
{
    if (!stmt.pathExpr || !stmt.channelExpr)
        return;

    RVal path = lowerExpr(*stmt.pathExpr);
    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    curLoc = stmt.loc;
    Value modeValue = emitUnary(Opcode::CastSiNarrowChk,
                                Type(Type::Kind::I32),
                                Value::constInt(static_cast<int32_t>(stmt.mode)));

    Value err = emitCallRet(Type(Type::Kind::I32),
                            "rt_open_err_vstr",
                            {path.value, modeValue, channel.value});

    emitRuntimeErrCheck(err, stmt.loc, "open", [&](Value code) {
        emitTrapFromErr(code);
    });
}

/// @brief Lower a CLOSE statement that releases an open channel.
///
/// @details The helper lowers and normalises the channel expression, then calls
///          `rt_close_err` to request runtime cleanup.  Non-zero error codes are
///          converted into traps through @ref emitRuntimeErrCheck, matching the
///          behaviour of other I/O statements.
///
/// @param stmt AST node representing the CLOSE statement.
void Lowerer::lowerClose(const CloseStmt &stmt)
{
    if (!stmt.channelExpr)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    curLoc = stmt.loc;
    Value err = emitCallRet(Type(Type::Kind::I32), "rt_close_err", {channel.value});

    emitRuntimeErrCheck(err, stmt.loc, "close", [&](Value code) {
        emitTrapFromErr(code);
    });
}

/// @brief Lower a SEEK statement that repositions a file handle.
///
/// @details Both channel and position expressions are lowered before coercing
///          the channel to 32 bits and the position to 64 bits.  The helper then
///          calls `rt_seek_ch_err` and routes the result through
///          @ref emitRuntimeErrCheck to surface runtime failures consistently.
///
/// @param stmt AST node describing the SEEK operation.
void Lowerer::lowerSeek(const SeekStmt &stmt)
{
    if (!stmt.channelExpr || !stmt.positionExpr)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    RVal position = lowerExpr(*stmt.positionExpr);
    position = ensureI64(std::move(position), stmt.loc);

    curLoc = stmt.loc;
    Value err = emitCallRet(Type(Type::Kind::I32),
                            "rt_seek_ch_err",
                            {channel.value, position.value});

    emitRuntimeErrCheck(err, stmt.loc, "seek", [&](Value code) {
        emitTrapFromErr(code);
    });
}

/// @brief Lower a PRINT statement targeting the default output stream.
///
/// @details The routine walks each print item, lowering the underlying
///          expression and dispatching to the appropriate runtime helper:
///          strings call `rt_print_str`, floating-point values call
///          `rt_print_f64`, and other numeric types are coerced to integers
///          before invoking `rt_print_i64`.  Comma items emit a space literal
///          while semicolons suppress newline emission.  Unless the final item is
///          a semicolon the helper appends an explicit newline literal to mirror
///          the BASIC runtime.
///
/// @param stmt AST node capturing the PRINT statement contents.
void Lowerer::lowerPrint(const PrintStmt &stmt)
{
    for (const auto &it : stmt.items)
    {
        switch (it.kind)
        {
            case PrintItem::Kind::Expr:
            {
                RVal value = lowerExpr(*it.expr);
                curLoc = stmt.loc;
                if (value.type.kind == Type::Kind::Str)
                {
                    emitCall("rt_print_str", {value.value});
                    break;
                }
                if (value.type.kind == Type::Kind::F64)
                {
                    emitCall("rt_print_f64", {value.value});
                    break;
                }
                value = lowerScalarExpr(std::move(value), stmt.loc);
                emitCall("rt_print_i64", {value.value});
                break;
            }
            case PrintItem::Kind::Comma:
            {
                std::string spaceLbl = getStringLabel(" ");
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
    }
}

/// @brief Convert a PRINT # argument into a heap-allocated string.
///
/// @details String operands optionally receive CSV quoting via
///          `rt_csv_quote_alloc`.  Numeric operands are classified using
///          @ref TypeRules and coerced to the runtime width required by the
///          conversion helper (narrowing integers or widening to double as
///          appropriate).  The selected runtime feature flag is returned to
///          allow callers to request extern declarations lazily.
///
/// @param expr          Source expression being converted.
/// @param value         Lowered value paired with its IL type.
/// @param quoteStrings  Whether to CSV-quote string operands.
/// @return String value ready for emission along with an optional feature flag.
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

    auto narrowInteger = [&](Type::Kind target) {
        value = ensureI64(std::move(value), expr.loc);
        curLoc = expr.loc;
        value.value = emitUnary(Opcode::CastSiNarrowChk, Type(target), value.value);
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

/// @brief Build the comma-separated record used by WRITE # statements.
///
/// @details Arguments are lowered to strings, requesting any runtime helpers
///          necessary for conversions.  The method concatenates arguments using
///          `rt_concat`, inserting comma separators between fields.  When the
///          statement carries no arguments, the routine falls back to an empty
///          string literal so the runtime still receives a valid payload.
///
/// @param stmt Statement currently being lowered.
/// @return String value containing the rendered record.
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

/// @brief Lower PRINT # and WRITE # statements for file channels.
///
/// @details After normalising the channel expression, the helper either emits
///          newline-only writes for empty argument lists or forwards actual
///          arguments through @ref lowerPrintChArgToString.  WRITE mode
///          consolidates arguments into a single CSV record, whereas PRINT mode
///          streams each argument individually.  Runtime calls are wrapped with
///          @ref emitRuntimeErrCheck to surface failures through traps with
///          consistent messaging.
///
/// @param stmt AST node representing the PRINT # or WRITE # statement.
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
            Value err = emitCallRet(Type(Type::Kind::I32), "rt_println_ch_err", {channel.value, empty});
            const char *context = isWrite ? "write" : "printch";
            emitRuntimeErrCheck(err, stmt.loc, context, [&](Value code) {
                emitTrapFromErr(code);
            });
        }
        return;
    }

    if (isWrite)
    {
        Value record = buildPrintChWriteRecord(stmt);
        curLoc = stmt.loc;
        Value err = emitCallRet(Type(Type::Kind::I32), "rt_println_ch_err", {channel.value, record});
        emitRuntimeErrCheck(err, stmt.loc, "write", [&](Value code) {
            emitTrapFromErr(code);
        });
        return;
    }

    for (const auto &arg : stmt.args)
    {
        if (!arg)
            continue;
        RVal value = lowerExpr(*arg);
        PrintChArgString lowered = lowerPrintChArgToString(*arg, std::move(value), false);
        if (lowered.feature)
            requestHelper(*lowered.feature);
        curLoc = arg->loc;
        Value err = emitCallRet(Type(Type::Kind::I32), "rt_println_ch_err", {channel.value, lowered.text});
        emitRuntimeErrCheck(err, arg->loc, "printch", [&](Value code) {
            emitTrapFromErr(code);
        });
    }
}

/// @brief Lower an INPUT statement that consumes user input from stdin.
///
/// @details Optional prompt strings are emitted before reading the line via
///          `rt_input_line`.  Single-variable statements store the raw line,
///          while multi-variable statements split the line into CSV fields via
///          `rt_split_fields`.  Fields are converted to the destination slot's
///          type, emitting reference-count management calls for string targets
///          and marking implicit conversions for numeric mismatches.
///
/// @param stmt AST node describing the INPUT statement.
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

    auto storeField = [&](const std::string &name, Value field) {
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
    emitCallRet(Type(Type::Kind::I64), "rt_split_fields", {line, fields, Value::constInt(fieldCount)});
    requireStrReleaseMaybe();
    curLoc = stmt.loc;
    emitCall("rt_str_release_maybe", {line});

    for (std::size_t i = 0; i < stmt.vars.size(); ++i)
    {
        long long offset = static_cast<long long>(i * 8);
        Value slot = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), fields, Value::constInt(offset));
        Value field = emitLoad(Type(Type::Kind::Str), slot);
        storeField(stmt.vars[i], field);
    }
}

/// @brief Lower an INPUT # statement that parses a single value from a channel.
///
/// @details The routine allocates temporary storage for the runtime output,
///          invokes `rt_line_input_ch_err`, and checks for errors.  The returned
///          string is split into one field, which is then converted into the
///          destination variable's type using dedicated parsing helpers.  Parsed
///          strings are released after use to honour runtime ownership
///          conventions.
///
/// @param stmt AST node describing the INPUT # statement.
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
        emitRuntimeErrCheck(err, stmt.loc, "inputch_parse", [&](Value code) { emitTrapFromErr(code); });
        Value parsed = emitLoad(Type(Type::Kind::F64), parsedSlot);
        curLoc = stmt.loc;
        emitStore(Type(Type::Kind::F64), slot, parsed);
    }
    else
    {
        Value err = emitCallRet(Type(Type::Kind::I32), "rt_parse_int64", {fieldCstr, parsedSlot});
        emitRuntimeErrCheck(err, stmt.loc, "inputch_parse", [&](Value code) { emitTrapFromErr(code); });
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

/// @brief Lower a LINE INPUT # statement that reads an entire line verbatim.
///
/// @details The helper lowers the channel expression, invokes
///          `rt_line_input_ch_err`, and traps on failure.  Successful reads store
///          the resulting string into the target variable when it is a simple
///          variable expression; other forms were rejected during semantic
///          analysis.  No further parsing occurs so the line content is preserved
///          exactly as delivered by the runtime.
///
/// @param stmt AST node representing the LINE INPUT # statement.
void Lowerer::lowerLineInputCh(const LineInputChStmt &stmt)
{
    if (!stmt.channelExpr || !stmt.targetVar)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    curLoc = stmt.loc;
    Value outSlot = emitAlloca(8);
    emitStore(Type(Type::Kind::Ptr), outSlot, Value::null());

    Value err = emitCallRet(Type(Type::Kind::I32), "rt_line_input_ch_err", {channel.value, outSlot});

    emitRuntimeErrCheck(err, stmt.loc, "lineinputch", [&](Value code) {
        emitTrapFromErr(code);
    });

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

