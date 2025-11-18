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
                            // Map to first two params only for now.
                            if (bb.params.size() >= 1 && rv.id == bb.params[0].id)
                            {
                                // x0 already holds param0 per ABI; nothing to emit.
                                goto after_body;
                            }
                            if (bb.params.size() >= 2 && rv.id == bb.params[1].id)
                            {
                                // Move param1 (x1) into return register x0
                                emitter.emitMovRR(out, PhysReg::X0, PhysReg::X1);
                                goto after_body;
                            }
                        }
                    }
                }
                // Pattern A: single block, penultimate add/sub/mul feeding ret, both operands are entry params 0/1
                if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && bb.params.size() >= 2)
                {
                    const auto &addI = bb.instructions[bb.instructions.size() - 2];
                    const auto &retI = bb.instructions.back();
                    if ((addI.op == il::core::Opcode::Add || addI.op == il::core::Opcode::IAddOvf ||
                         addI.op == il::core::Opcode::Sub || addI.op == il::core::Opcode::ISubOvf ||
                         addI.op == il::core::Opcode::Mul || addI.op == il::core::Opcode::IMulOvf) &&
                        retI.op == il::core::Opcode::Ret && addI.result && !retI.operands.empty())
                    {
                        const auto &retV = retI.operands[0];
                        if (retV.kind == il::core::Value::Kind::Temp && retV.id == *addI.result &&
                            addI.operands.size() == 2 &&
                            addI.operands[0].kind == il::core::Value::Kind::Temp &&
                            addI.operands[1].kind == il::core::Value::Kind::Temp)
                        {
                            const unsigned p0 = bb.params[0].id;
                            const unsigned p1 = bb.params[1].id;
                            const unsigned o0 = addI.operands[0].id;
                            const unsigned o1 = addI.operands[1].id;
                            if ((o0 == p0 && o1 == p1) || (o0 == p1 && o1 == p0))
                            {
                                if (addI.op == il::core::Opcode::Add || addI.op == il::core::Opcode::IAddOvf)
                                    emitter.emitAddRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1);
                                else if (addI.op == il::core::Opcode::Sub || addI.op == il::core::Opcode::ISubOvf)
                                    emitter.emitSubRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1);
                                else
                                    emitter.emitMulRRR(out, PhysReg::X0, PhysReg::X0, PhysReg::X1);
                                goto after_body;
                            }
                        }
                    }
                }

                // Pattern A.1: single block, penultimate add/sub with one entry param and one immediate
                if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && !bb.params.empty())
                {
                    const auto &binI = bb.instructions[bb.instructions.size() - 2];
                    const auto &retI = bb.instructions.back();
                    const bool isAdd = (binI.op == il::core::Opcode::Add || binI.op == il::core::Opcode::IAddOvf);
                    const bool isSub = (binI.op == il::core::Opcode::Sub || binI.op == il::core::Opcode::ISubOvf);
                    if ((isAdd || isSub) && retI.op == il::core::Opcode::Ret && binI.result &&
                        !retI.operands.empty() && binI.operands.size() == 2)
                    {
                        const auto &retV = retI.operands[0];
                        if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
                        {
                            const auto &o0 = binI.operands[0];
                            const auto &o1 = binI.operands[1];
                            auto emitImm = [&](unsigned paramIndex, long long imm)
                            {
                                if (paramIndex == 0)
                                {
                                    if (isAdd) emitter.emitAddRI(out, PhysReg::X0, PhysReg::X0, imm);
                                    else emitter.emitSubRI(out, PhysReg::X0, PhysReg::X0, imm);
                                }
                                else if (paramIndex == 1)
                                {
                                    emitter.emitMovRR(out, PhysReg::X0, PhysReg::X1);
                                    if (isAdd) emitter.emitAddRI(out, PhysReg::X0, PhysReg::X0, imm);
                                    else emitter.emitSubRI(out, PhysReg::X0, PhysReg::X0, imm);
                                }
                            };
                            if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
                            {
                                if (bb.params.size() >= 1 && o0.id == bb.params[0].id)
                                {
                                    emitImm(0, o1.i64);
                                    goto after_body;
                                }
                                if (bb.params.size() >= 2 && o0.id == bb.params[1].id)
                                {
                                    emitImm(1, o1.i64);
                                    goto after_body;
                                }
                            }
                            if (o1.kind == il::core::Value::Kind::Temp && o0.kind == il::core::Value::Kind::ConstInt)
                            {
                                if (bb.params.size() >= 1 && o1.id == bb.params[0].id)
                                {
                                    // Note: for sub with reversed operands (imm - param), skip
                                    if (isAdd) { emitImm(0, o0.i64); goto after_body; }
                                }
                                if (bb.params.size() >= 2 && o1.id == bb.params[1].id)
                                {
                                    if (isAdd) { emitImm(1, o0.i64); goto after_body; }
                                }
                            }
                        }
                    }
                }

                // Pattern B: ret immediate small integer
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
                                emitter.emitMovRI(out, PhysReg::X0, v.i64);
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
