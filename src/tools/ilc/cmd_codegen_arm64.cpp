//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/ilc/cmd_codegen_arm64.cpp
// Purpose: Minimal CLI glue for `ilc codegen arm64 -S` using AArch64 AsmEmitter.
//
//===----------------------------------------------------------------------===//

#include "cmd_codegen_arm64.hpp"

#include "codegen/aarch64/AsmEmitter.hpp"
#include "tools/common/module_loader.hpp"
#include "codegen/common/ArgNormalize.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace viper::tools::ilc
{
namespace
{

using il::core::Opcode;

// Forward declaration for predicate→condition mapper used by emitAssembly.
static const char *condForOpcode(Opcode op);

constexpr std::string_view kUsage =
    "usage: ilc codegen arm64 <file.il> -S <file.s>\n";

struct ArgvView
{
    int argc;
    char **argv;
    [[nodiscard]] bool empty() const { return argc <= 0 || argv == nullptr; }
    [[nodiscard]] std::string_view front() const { return empty() ? std::string_view{} : argv[0]; }
    [[nodiscard]] std::string_view at(int i) const
    {
        if (i < 0 || i >= argc || argv == nullptr) return std::string_view{};
        return argv[i];
    }
    [[nodiscard]] ArgvView drop_front(int n = 1) const
    {
        if (n >= argc) return {0, nullptr};
        return {argc - n, argv + n};
    }
};

struct Options
{
    std::string input_il;
    std::optional<std::string> output_s;
};

std::optional<Options> parseArgs(const ArgvView &args)
{
    if (args.empty())
    {
        std::cerr << kUsage;
        return std::nullopt;
    }
    Options opts;
    opts.input_il = std::string(args.front());
    for (int i = 1; i < args.argc; ++i)
    {
        const std::string_view tok = args.at(i);
        if (tok == "-S")
        {
            if (i + 1 >= args.argc)
            {
                std::cerr << "error: -S requires an output path\n" << kUsage;
                return std::nullopt;
            }
            opts.output_s = std::string(args.at(++i));
            continue;
        }
        std::cerr << "error: unknown flag '" << tok << "'\n" << kUsage;
        return std::nullopt;
    }
    if (!opts.output_s)
    {
        std::cerr << "error: -S is required for arm64 backend\n" << kUsage;
        return std::nullopt;
    }
    return opts;
}

int emitAssembly(const Options &opts)
{
    il::core::Module mod;
    const auto load = il::tools::common::loadModuleFromFile(opts.input_il, mod, std::cerr);
    if (!load.succeeded())
    {
        return 1;
    }

    // Open output stream
    std::ofstream out(*opts.output_s);
    if (!out)
    {
        std::cerr << "unable to open " << *opts.output_s << "\n";
        return 1;
    }

    // Small helpers to normalize args and emit rr/ri forms
    auto indexOfParam = [](const il::core::BasicBlock &bb, unsigned tempId) -> int {
        for (size_t i = 0; i < bb.params.size(); ++i)
            if (bb.params[i].id == tempId) return static_cast<int>(i);
        return -1;
    };

    // (Note) additional helpers can be introduced as needed to reduce duplication.

    // Emit a trivial function stub for each IL function.
    using namespace viper::codegen::aarch64;
    auto &ti = darwinTarget();
    AsmEmitter emitter{ti};
    for (const auto &fn : mod.functions)
    {
        emitter.emitFunctionHeader(out, fn.name);
        emitter.emitPrologue(out);
        // Minimal lowering: handle simple patterns for i64 returns
        if (fn.retType.kind == il::core::Type::Kind::I64)
        {
            if (!fn.blocks.empty())
            {
                const auto &bb = fn.blocks.front();
                // Fast path: ret of an entry parameter
                if (fn.blocks.size() == 1 && !bb.instructions.empty() && !bb.params.empty())
                {
                    const auto &retI = bb.instructions.back();
                    if (retI.op == il::core::Opcode::Ret && !retI.operands.empty())
                    {
                        const auto &rv = retI.operands[0];
                        if (rv.kind == il::core::Value::Kind::Temp)
                        {
                            // Generic: ret %paramN for any N in range
                            int pIdx = indexOfParam(bb, rv.id);
                            if (pIdx >= 0)
                            {
                                const auto &argOrder = ti.intArgOrder;
                                const PhysReg src = argOrder[static_cast<size_t>(pIdx)];
                                if (src != PhysReg::X0)
                                    emitter.emitMovRR(out, PhysReg::X0, src);
                                goto after_body;
                            }
                        }
                    }
                }
                // Pattern A: single block, penultimate rr op feeding ret, both operands are entry params (indices 0..7)
                if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && bb.params.size() >= 2)
                {
                    const auto &addI = bb.instructions[bb.instructions.size() - 2];
                    const auto &retI = bb.instructions.back();
                    if ((addI.op == il::core::Opcode::Add || addI.op == il::core::Opcode::IAddOvf ||
                         addI.op == il::core::Opcode::Sub || addI.op == il::core::Opcode::ISubOvf ||
                         addI.op == il::core::Opcode::Mul || addI.op == il::core::Opcode::IMulOvf ||
                         addI.op == il::core::Opcode::And || addI.op == il::core::Opcode::Or ||
                         addI.op == il::core::Opcode::Xor ||
                         addI.op == il::core::Opcode::ICmpEq || addI.op == il::core::Opcode::ICmpNe ||
                         addI.op == il::core::Opcode::SCmpLT || addI.op == il::core::Opcode::SCmpLE ||
                         addI.op == il::core::Opcode::SCmpGT || addI.op == il::core::Opcode::SCmpGE ||
                         addI.op == il::core::Opcode::UCmpLT || addI.op == il::core::Opcode::UCmpLE ||
                         addI.op == il::core::Opcode::UCmpGT || addI.op == il::core::Opcode::UCmpGE) &&
                        retI.op == il::core::Opcode::Ret && addI.result && !retI.operands.empty())
                    {
                        const auto &retV = retI.operands[0];
                        if (retV.kind == il::core::Value::Kind::Temp && retV.id == *addI.result &&
                            addI.operands.size() == 2 &&
                            addI.operands[0].kind == il::core::Value::Kind::Temp &&
                            addI.operands[1].kind == il::core::Value::Kind::Temp)
                        {
                            const int idx0 = indexOfParam(bb, addI.operands[0].id);
                            const int idx1 = indexOfParam(bb, addI.operands[1].id);
                            if (idx0 >= 0 && idx1 >= 0 && idx0 < 8 && idx1 < 8)
                            {
                                // Normalize param regs to (x0,x1) using x9 scratch
                                viper::codegen::common::normalize_rr_to_x0_x1(emitter, ti,
                                                                               static_cast<std::size_t>(idx0),
                                                                               static_cast<std::size_t>(idx1),
                                                                               PhysReg::X9, PhysReg::X0, PhysReg::X1, out);
                                switch (addI.op)
                                {
                                    case Opcode::Add: case Opcode::IAddOvf:
                                        emitter.emitAddRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                    case Opcode::Sub: case Opcode::ISubOvf:
                                        emitter.emitSubRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                    case Opcode::Mul: case Opcode::IMulOvf:
                                        emitter.emitMulRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                    case Opcode::And:
                                        emitter.emitAndRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                    case Opcode::Or:
                                        emitter.emitOrrRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                    case Opcode::Xor:
                                        emitter.emitEorRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                    case Opcode::ICmpEq: case Opcode::ICmpNe:
                                    case Opcode::SCmpLT: case Opcode::SCmpLE: case Opcode::SCmpGT: case Opcode::SCmpGE:
                                    case Opcode::UCmpLT: case Opcode::UCmpLE: case Opcode::UCmpGT: case Opcode::UCmpGE:
                                        emitter.emitCmpRR(out, PhysReg::X0, PhysReg::X1);
                                        emitter.emitCset(out, PhysReg::X0, condForOpcode(addI.op));
                                        break;
                                    default: break;
                                }
                                goto after_body;
                            }
                        }
                    }
                }

                // Pattern A.1: single block, penultimate add/sub/shifts with one entry param (index 0..7) and one immediate
                if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && !bb.params.empty())
                {
                    const auto &binI = bb.instructions[bb.instructions.size() - 2];
                    const auto &retI = bb.instructions.back();
                    const bool isAdd = (binI.op == il::core::Opcode::Add || binI.op == il::core::Opcode::IAddOvf);
                    const bool isSub = (binI.op == il::core::Opcode::Sub || binI.op == il::core::Opcode::ISubOvf);
                    const bool isShl = (binI.op == il::core::Opcode::Shl);
                    const bool isLShr = (binI.op == il::core::Opcode::LShr);
                    const bool isAShr = (binI.op == il::core::Opcode::AShr);
                    const bool isICmpImm = (condForOpcode(binI.op) != nullptr);
                    if ((isAdd || isSub || isShl || isLShr || isAShr) && retI.op == il::core::Opcode::Ret && binI.result &&
                        !retI.operands.empty() && binI.operands.size() == 2)
                    {
                        const auto &retV = retI.operands[0];
                        if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
                        {
                            const auto &o0 = binI.operands[0];
                            const auto &o1 = binI.operands[1];
                            auto emitImm = [&](unsigned paramIndex, long long imm)
                            {
                                const auto &argOrder = ti.intArgOrder;
                                if (paramIndex < argOrder.size())
                                {
                                    const PhysReg src = argOrder[paramIndex];
                                    emitter.emitMovRR(out, PhysReg::X0, src);
                                    if (isAdd) emitter.emitAddRI(out, PhysReg::X0, PhysReg::X0, imm);
                                    else if (isSub) emitter.emitSubRI(out, PhysReg::X0, PhysReg::X0, imm);
                                    else if (isShl) emitter.emitLslRI(out, PhysReg::X0, PhysReg::X0, imm);
                                    else if (isLShr) emitter.emitLsrRI(out, PhysReg::X0, PhysReg::X0, imm);
                                    else if (isAShr) emitter.emitAsrRI(out, PhysReg::X0, PhysReg::X0, imm);
                                }
                            };
                            if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
                            {
                                for (size_t i = 0; i < bb.params.size(); ++i)
                                {
                                    if (bb.params[i].id == o0.id && i < 8)
                                    {
                                        emitImm(static_cast<unsigned>(i), o1.i64);
                                        goto after_body;
                                    }
                                }
                            }
                            if (o1.kind == il::core::Value::Kind::Temp && o0.kind == il::core::Value::Kind::ConstInt)
                            {
                                // Only support commutative/add or shifts when imm is on lhs
                                if (isAdd || isShl || isLShr || isAShr)
                                {
                                    for (size_t i = 0; i < bb.params.size(); ++i)
                                    {
                                        if (bb.params[i].id == o1.id && i < 8)
                                        {
                                            emitImm(static_cast<unsigned>(i), o0.i64);
                                            goto after_body;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // Handle immediate compares: param vs const → cmp x0, #imm; cset
                    if (isICmpImm && retI.op == il::core::Opcode::Ret && binI.result && !retI.operands.empty() && binI.operands.size() == 2)
                    {
                        const auto &retV = retI.operands[0];
                        if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
                        {
                            const auto &o0 = binI.operands[0];
                            const auto &o1 = binI.operands[1];
                            auto emitCmpImm = [&](unsigned paramIndex, long long imm)
                            {
                                const auto &argOrder = ti.intArgOrder;
                                if (paramIndex < argOrder.size())
                                {
                                    const PhysReg src = argOrder[paramIndex];
                                    if (src != PhysReg::X0) emitter.emitMovRR(out, PhysReg::X0, src);
                                    emitter.emitCmpRI(out, PhysReg::X0, imm);
                                    emitter.emitCset(out, PhysReg::X0, condForOpcode(binI.op));
                                }
                            };
                            if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
                            {
                                for (size_t i = 0; i < bb.params.size(); ++i)
                                {
                                    if (bb.params[i].id == o0.id && i < 8)
                                    {
                                        emitCmpImm(static_cast<unsigned>(i), o1.i64);
                                        goto after_body;
                                    }
                                }
                            }
                            // (const vs param) forms require swapping operands and inverting predicate; omitted for now.
                        }
                    }
                }

                // Pattern B: ret immediate integer (materialize if needed)
                for (const auto &block : fn.blocks)
                {
                    if (!block.instructions.empty())
                    {
                        const auto &term = block.instructions.back();
                        if (term.op == il::core::Opcode::Ret && !term.operands.empty())
                        {
                            const auto &v = term.operands[0];
                            if (v.kind == il::core::Value::Kind::ConstInt)
                            {
                                const long long imm = v.i64;
                                if (imm >= 0 && imm <= 65535)
                                {
                                    emitter.emitMovRI(out, PhysReg::X0, imm);
                                }
                                else
                                {
                                    emitter.emitMovImm64(out, PhysReg::X0, static_cast<unsigned long long>(imm));
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    after_body:
        emitter.emitEpilogue(out);
        out << "\n";
    }
    return 0;
}

// Map IL integer-compare opcodes to AArch64 condition code strings for CSET.
static const char *condForOpcode(Opcode op)
{
    switch (op)
    {
        case Opcode::ICmpEq: return "eq";
        case Opcode::ICmpNe: return "ne";
        case Opcode::SCmpLT: return "lt";
        case Opcode::SCmpLE: return "le";
        case Opcode::SCmpGT: return "gt";
        case Opcode::SCmpGE: return "ge";
        case Opcode::UCmpLT: return "lo";
        case Opcode::UCmpLE: return "ls";
        case Opcode::UCmpGT: return "hi";
        case Opcode::UCmpGE: return "hs";
        default: return nullptr;
    }
}

} // namespace

int cmd_codegen_arm64(int argc, char **argv)
{
    const ArgvView args{argc, argv};
    auto parsed = parseArgs(args);
    if (!parsed)
    {
        return 1;
    }
    return emitAssembly(*parsed);
}

} // namespace viper::tools::ilc
