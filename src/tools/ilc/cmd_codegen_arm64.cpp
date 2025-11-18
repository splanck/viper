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

// Small helper that encapsulates the pattern-based lowering used by the arm64 CLI.
// Keeps cmd driver tidy and centralizes opcode/sequence mapping.
class Arm64PatternLowerer
{
  public:
    Arm64PatternLowerer(const viper::codegen::aarch64::TargetInfo &ti,
                        viper::codegen::aarch64::AsmEmitter &emitter) noexcept
        : ti_(ti), emitter_(emitter) {}

    bool lowerFunction(const il::core::Function &fn, std::ostream &out)
    {
        using namespace viper::codegen::aarch64;
        emitter_.emitFunctionHeader(out, fn.name);
        emitter_.emitPrologue(out);

        // Pattern A: rr ops (including compares) on entry params feeding ret
        for (const auto &bb : fn.blocks)
        {
            if (bb.instructions.size() >= 2 && !bb.params.empty())
            {
                const auto &opI = bb.instructions[bb.instructions.size() - 2];
                const auto &retI = bb.instructions.back();
                if ((opI.op == il::core::Opcode::Add || opI.op == il::core::Opcode::IAddOvf ||
                     opI.op == il::core::Opcode::Sub || opI.op == il::core::Opcode::ISubOvf ||
                     opI.op == il::core::Opcode::Mul || opI.op == il::core::Opcode::IMulOvf ||
                     opI.op == il::core::Opcode::And || opI.op == il::core::Opcode::Or ||
                     opI.op == il::core::Opcode::Xor ||
                     opI.op == il::core::Opcode::ICmpEq || opI.op == il::core::Opcode::ICmpNe ||
                     opI.op == il::core::Opcode::SCmpLT || opI.op == il::core::Opcode::SCmpLE ||
                     opI.op == il::core::Opcode::SCmpGT || opI.op == il::core::Opcode::SCmpGE ||
                     opI.op == il::core::Opcode::UCmpLT || opI.op == il::core::Opcode::UCmpLE ||
                     opI.op == il::core::Opcode::UCmpGT || opI.op == il::core::Opcode::UCmpGE) &&
                    retI.op == il::core::Opcode::Ret && opI.result && !retI.operands.empty())
                {
                    const auto &retV = retI.operands[0];
                    if (retV.kind == il::core::Value::Kind::Temp && retV.id == *opI.result &&
                        opI.operands.size() == 2 && opI.operands[0].kind == il::core::Value::Kind::Temp &&
                        opI.operands[1].kind == il::core::Value::Kind::Temp)
                    {
                        const int idx0 = indexOfParam(bb, opI.operands[0].id);
                        const int idx1 = indexOfParam(bb, opI.operands[1].id);
                        if (idx0 >= 0 && idx1 >= 0 && idx0 < 8 && idx1 < 8)
                        {
                            viper::codegen::common::normalize_rr_to_x0_x1(
                                emitter_, ti_, static_cast<std::size_t>(idx0), static_cast<std::size_t>(idx1),
                                PhysReg::X9, PhysReg::X0, PhysReg::X1, out);
                            switch (opI.op)
                            {
                                case Opcode::Add:
                                case Opcode::IAddOvf: emitter_.emitAddRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                case Opcode::Sub:
                                case Opcode::ISubOvf: emitter_.emitSubRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                case Opcode::Mul:
                                case Opcode::IMulOvf: emitter_.emitMulRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                case Opcode::And: emitter_.emitAndRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                case Opcode::Or: emitter_.emitOrrRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                case Opcode::Xor: emitter_.emitEorRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1); break;
                                case Opcode::ICmpEq:
                                case Opcode::ICmpNe:
                                case Opcode::SCmpLT:
                                case Opcode::SCmpLE:
                                case Opcode::SCmpGT:
                                case Opcode::SCmpGE:
                                case Opcode::UCmpLT:
                                case Opcode::UCmpLE:
                                case Opcode::UCmpGT:
                                case Opcode::UCmpGE:
                                    emitter_.emitCmpRR(out, PhysReg::X0, PhysReg::X1);
                                    emitter_.emitCset(out, PhysReg::X0, condForOpcode(opI.op));
                                    break;
                                default: break;
                            }
                            goto after_body;
                        }
                    }
                }
            }

            // Pattern A.1: single block ri/shifts and compares against immediates
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
                            const auto &argOrder = ti_.intArgOrder;
                            if (paramIndex < argOrder.size())
                            {
                                const PhysReg src = argOrder[paramIndex];
                                emitter_.emitMovRR(out, PhysReg::X0, src);
                                if (isAdd) emitter_.emitAddRI(out, PhysReg::X0, PhysReg::X0, imm);
                                else if (isSub) emitter_.emitSubRI(out, PhysReg::X0, PhysReg::X0, imm);
                                else if (isShl) emitter_.emitLslRI(out, PhysReg::X0, PhysReg::X0, imm);
                                else if (isLShr) emitter_.emitLsrRI(out, PhysReg::X0, PhysReg::X0, imm);
                                else if (isAShr) emitter_.emitAsrRI(out, PhysReg::X0, PhysReg::X0, imm);
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
                if (isICmpImm && retI.op == il::core::Opcode::Ret && binI.result && !retI.operands.empty() &&
                    binI.operands.size() == 2)
                {
                    const auto &retV = retI.operands[0];
                    if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
                    {
                        const auto &o0 = binI.operands[0];
                        const auto &o1 = binI.operands[1];
                        auto emitCmpImm = [&](unsigned paramIndex, long long imm)
                        {
                            const auto &argOrder = ti_.intArgOrder;
                            if (paramIndex < argOrder.size())
                            {
                                const PhysReg src = argOrder[paramIndex];
                                if (src != PhysReg::X0) emitter_.emitMovRR(out, PhysReg::X0, src);
                                emitter_.emitCmpRI(out, PhysReg::X0, imm);
                                emitter_.emitCset(out, PhysReg::X0, condForOpcode(binI.op));
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
                    }
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
                            emitter_.emitMovRI(out, PhysReg::X0, imm);
                        }
                        else
                        {
                            emitter_.emitMovImm64(out, PhysReg::X0, static_cast<unsigned long long>(imm));
                        }
                        break;
                    }
                }
            }
        }

    after_body:
        emitter_.emitEpilogue(out);
        out << "\n";
        return true;
    }

  private:
    static int indexOfParam(const il::core::BasicBlock &bb, unsigned tempId)
    {
        for (size_t i = 0; i < bb.params.size(); ++i)
            if (bb.params[i].id == tempId) return static_cast<int>(i);
        return -1;
    }

    const viper::codegen::aarch64::TargetInfo &ti_;
    viper::codegen::aarch64::AsmEmitter &emitter_;
};

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

    using namespace viper::codegen::aarch64;
    auto &ti = darwinTarget();
    AsmEmitter emitter{ti};
    Arm64PatternLowerer lowerer{ti, emitter};
    for (const auto &fn : mod.functions)
    {
        lowerer.lowerFunction(fn, out);
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
