// File: src/frontends/basic/LowerStmt_IO.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements BASIC statement lowering helpers focused on terminal and
//          file I/O operations.
// Key invariants: Helpers reuse the active Lowerer context, ensuring channels
//                 are normalized and runtime error checks remain consistent.
// Ownership/Lifetime: Operates on Lowerer without owning AST or IL modules.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

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

