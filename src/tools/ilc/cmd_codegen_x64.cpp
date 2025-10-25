// File: src/tools/ilc/cmd_codegen_x64.cpp
// Purpose: Implement the ilc glue that lowers IL modules via the x86-64 backend.
// Key invariants: Emits diagnostics on I/O or backend failures and never leaves
//                 partially written output files on error paths.
// Ownership/Lifetime: Borrows parsed IL modules and writes assembly/binaries to
//                     caller-specified locations.
// Links: src/codegen/x86_64/Backend.hpp, src/tools/common/module_loader.hpp

// TODO(PhaseB): Extend the adapter to cover division, overflow arithmetic, logic,
//               switches, GEP/addressing, runtime/EH ops, and remaining cast forms.

#include "cmd_codegen_x64.hpp"

#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/Unsupported.hpp"
#include "il/core/Module.hpp"
#include "tools/common/module_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace viper::tools::ilc
{
namespace
{

/// @brief Configuration derived from the command line.
struct CodegenConfig
{
    std::string inputPath;                       ///< IL file to compile.
    std::optional<std::string> assemblyPath{};   ///< Optional explicit assembly output path.
    std::optional<std::string> executablePath{}; ///< Optional explicit executable path.
    bool runNative{false};                       ///< Request execution of the produced binary.
};

/// @brief Print a concise usage hint for the subcommand.
void printUsageHint()
{
    std::cerr << "usage: ilc codegen x64 <file.il> [-S <file.s>] [-o <a.out>] [-run-native]\n";
}

/// @brief Decode the system() return value into an exit status when possible.
int normaliseSystemStatus(int status)
{
    if (status == -1)
    {
        return -1;
    }
#if defined(_WIN32)
    return status;
#else
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }
    return status;
#endif
}

/// @brief Convert the parsed IL module into the temporary adapter structure.
viper::codegen::x64::ILModule convertToAdapterModule(const il::core::Module &module)
{
    using viper::codegen::x64::ILBlock;
    using viper::codegen::x64::ILFunction;
    using viper::codegen::x64::ILModule;
    using viper::codegen::x64::ILValue;

    auto reportUnsupported = [](std::string detail) [[noreturn]]
    { viper::codegen::x64::phaseAUnsupported(detail.c_str()); };

    const auto typeToKind = [&reportUnsupported](const il::core::Type &type) -> ILValue::Kind
    {
        using il::core::Type;
        switch (type.kind)
        {
            case Type::Kind::I1:
                return ILValue::Kind::I1;
            case Type::Kind::I16:
            case Type::Kind::I32:
            case Type::Kind::I64:
                return ILValue::Kind::I64;
            case Type::Kind::F64:
                return ILValue::Kind::F64;
            case Type::Kind::Ptr:
                return ILValue::Kind::PTR;
            case Type::Kind::Str:
                return ILValue::Kind::STR;
            case Type::Kind::Void:
                reportUnsupported("void-typed value requested by backend adapter");
                break;
            case Type::Kind::Error:
            case Type::Kind::ResumeTok:
                reportUnsupported("non-scalar IL type encountered during Phase A lowering");
                break;
        }
        reportUnsupported("unknown IL type kind encountered during Phase A lowering");
    };

    const auto makeLabelValue = [](std::string name) -> ILValue
    {
        ILValue label{};
        label.kind = ILValue::Kind::LABEL;
        label.label = std::move(name);
        label.id = -1;
        return label;
    };

    const auto makeCondImmediate = [](int code) -> ILValue
    {
        ILValue imm{};
        imm.kind = ILValue::Kind::I64;
        imm.i64 = static_cast<long long>(code);
        imm.id = -1;
        return imm;
    };

    const auto condCodeFor = [](il::core::Opcode op) -> int
    {
        using il::core::Opcode;
        switch (op)
        {
            case Opcode::ICmpEq:
            case Opcode::FCmpEQ:
                return 0; // equal
            case Opcode::ICmpNe:
            case Opcode::FCmpNE:
                return 1; // not equal
            case Opcode::SCmpLT:
            case Opcode::FCmpLT:
                return 2; // less than (signed)
            case Opcode::SCmpLE:
            case Opcode::FCmpLE:
                return 3; // less equal (signed)
            case Opcode::SCmpGT:
            case Opcode::FCmpGT:
                return 4; // greater than (signed)
            case Opcode::SCmpGE:
            case Opcode::FCmpGE:
                return 5; // greater equal (signed)
            case Opcode::UCmpGT:
                return 6; // above
            case Opcode::UCmpGE:
                return 7; // above or equal
            case Opcode::UCmpLT:
                return 8; // below
            case Opcode::UCmpLE:
                return 9; // below or equal
            default:
                return 0;
        }
    };

    ILModule adapted{};
    adapted.funcs.reserve(module.functions.size());

    for (const auto &func : module.functions)
    {
        ILFunction adaptedFunc{};
        adaptedFunc.name = func.name;

        std::unordered_map<unsigned, ILValue::Kind> valueKinds{};
        valueKinds.reserve(func.valueNames.size() + func.params.size());

        for (const auto &param : func.params)
        {
            valueKinds.emplace(param.id, typeToKind(param.type));
        }

        for (const auto &block : func.blocks)
        {
            ILBlock adaptedBlock{};
            adaptedBlock.name = block.label;

            for (const auto &param : block.params)
            {
                const ILValue::Kind kind = typeToKind(param.type);
                adaptedBlock.paramIds.push_back(static_cast<int>(param.id));
                adaptedBlock.paramKinds.push_back(kind);
                valueKinds[param.id] = kind;
            }

            const auto convertValue = [&reportUnsupported,
                                       &valueKinds](const il::core::Value &value,
                                                    std::optional<ILValue::Kind> hint) -> ILValue
            {
                ILValue converted{};
                converted.id = -1;

                switch (value.kind)
                {
                    case il::core::Value::Kind::Temp:
                    {
                        const auto it = valueKinds.find(value.id);
                        if (it == valueKinds.end())
                        {
                            reportUnsupported(
                                "ssa temp without registered kind in Phase A lowering");
                        }
                        converted.kind = it->second;
                        converted.id = static_cast<int>(value.id);
                        break;
                    }
                    case il::core::Value::Kind::ConstInt:
                    {
                        converted.kind =
                            hint.value_or(value.isBool ? ILValue::Kind::I1 : ILValue::Kind::I64);
                        converted.i64 = value.i64;
                        break;
                    }
                    case il::core::Value::Kind::ConstFloat:
                        converted.kind = ILValue::Kind::F64;
                        converted.f64 = value.f64;
                        break;
                    case il::core::Value::Kind::ConstStr:
                        converted.kind = ILValue::Kind::STR;
                        converted.str = value.str;
                        converted.strLen = static_cast<std::uint64_t>(value.str.size());
                        break;
                    case il::core::Value::Kind::GlobalAddr:
                        converted.kind = ILValue::Kind::LABEL;
                        converted.label = value.str;
                        break;
                    case il::core::Value::Kind::NullPtr:
                        converted.kind = ILValue::Kind::PTR;
                        converted.i64 = 0;
                        break;
                }

                if (hint && value.kind != il::core::Value::Kind::Temp)
                {
                    converted.kind = *hint;
                }

                return converted;
            };

            const auto convertOperands =
                [&](const il::core::Instr &instr,
                    std::initializer_list<std::optional<ILValue::Kind>> hints,
                    viper::codegen::x64::ILInstr &out)
            {
                std::size_t index = 0;
                for (const auto &operand : instr.operands)
                {
                    const std::optional<ILValue::Kind> hint =
                        index < hints.size() ? *(hints.begin() + static_cast<std::ptrdiff_t>(index))
                                             : std::optional<ILValue::Kind>{};
                    out.ops.push_back(convertValue(operand, hint));
                    ++index;
                }
            };

            for (const auto &instr : block.instructions)
            {
                viper::codegen::x64::ILInstr adaptedInstr{};
                adaptedInstr.resultId = -1;

                const auto setResultKind = [&](const il::core::Type &type) -> ILValue::Kind
                {
                    const ILValue::Kind kind = typeToKind(type);
                    if (instr.result)
                    {
                        adaptedInstr.resultId = static_cast<int>(*instr.result);
                        adaptedInstr.resultKind = kind;
                        valueKinds[*instr.result] = kind;
                    }
                    else
                    {
                        adaptedInstr.resultKind = kind;
                    }
                    return kind;
                };

                switch (instr.op)
                {
                    case il::core::Opcode::Add:
                    case il::core::Opcode::FAdd:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "add";
                        convertOperands(instr, {kind, kind}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Sub:
                    case il::core::Opcode::FSub:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "sub";
                        convertOperands(instr, {kind, kind}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Mul:
                    case il::core::Opcode::FMul:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "mul";
                        convertOperands(instr, {kind, kind}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::SDiv:
                    case il::core::Opcode::SDivChk0:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "sdiv";
                        convertOperands(instr,
                                        {ILValue::Kind::I64, ILValue::Kind::I64},
                                        adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::SRem:
                    case il::core::Opcode::SRemChk0:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "srem";
                        convertOperands(instr,
                                        {ILValue::Kind::I64, ILValue::Kind::I64},
                                        adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Shl:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "shl";
                        convertOperands(instr, {kind, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::LShr:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "lshr";
                        convertOperands(instr, {kind, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::AShr:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "ashr";
                        convertOperands(instr, {kind, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::ICmpEq:
                    case il::core::Opcode::ICmpNe:
                    case il::core::Opcode::SCmpLT:
                    case il::core::Opcode::SCmpLE:
                    case il::core::Opcode::SCmpGT:
                    case il::core::Opcode::SCmpGE:
                    case il::core::Opcode::UCmpLT:
                    case il::core::Opcode::UCmpLE:
                    case il::core::Opcode::UCmpGT:
                    case il::core::Opcode::UCmpGE:
                    case il::core::Opcode::FCmpEQ:
                    case il::core::Opcode::FCmpNE:
                    case il::core::Opcode::FCmpLT:
                    case il::core::Opcode::FCmpLE:
                    case il::core::Opcode::FCmpGT:
                    case il::core::Opcode::FCmpGE:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "cmp";
                        const bool isFloat = instr.op == il::core::Opcode::FCmpEQ ||
                                             instr.op == il::core::Opcode::FCmpNE ||
                                             instr.op == il::core::Opcode::FCmpLT ||
                                             instr.op == il::core::Opcode::FCmpLE ||
                                             instr.op == il::core::Opcode::FCmpGT ||
                                             instr.op == il::core::Opcode::FCmpGE;
                        const ILValue::Kind operandKind =
                            isFloat ? ILValue::Kind::F64 : ILValue::Kind::I64;
                        convertOperands(instr, {operandKind, operandKind}, adaptedInstr);
                        adaptedInstr.ops.push_back(makeCondImmediate(condCodeFor(instr.op)));
                        break;
                    }
                    case il::core::Opcode::Select:
                    {
                        const ILValue::Kind resultKind = setResultKind(instr.type);
                        adaptedInstr.opcode = "select";
                        convertOperands(
                            instr, {ILValue::Kind::I1, resultKind, resultKind}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Call:
                    {
                        if (instr.result && instr.type.kind != il::core::Type::Kind::Void)
                        {
                            setResultKind(instr.type);
                        }
                        else if (instr.result)
                        {
                            reportUnsupported("void call returning SSA id in Phase A lowering");
                        }
                        adaptedInstr.opcode = "call";
                        adaptedInstr.ops.push_back(makeLabelValue(instr.callee));
                        for (const auto &operand : instr.operands)
                        {
                            adaptedInstr.ops.push_back(convertValue(operand, std::nullopt));
                        }
                        break;
                    }
                    case il::core::Opcode::Load:
                    {
                        const ILValue::Kind resultKind = setResultKind(instr.type);
                        adaptedInstr.opcode = "load";
                        convertOperands(
                            instr, {ILValue::Kind::PTR, ILValue::Kind::I64}, adaptedInstr);
                        if (adaptedInstr.ops.size() > 2)
                        {
                            adaptedInstr.ops.resize(2);
                        }
                        (void)resultKind;
                        break;
                    }
                    case il::core::Opcode::Store:
                    {
                        adaptedInstr.opcode = "store";
                        convertOperands(instr,
                                        {std::nullopt, ILValue::Kind::PTR, ILValue::Kind::I64},
                                        adaptedInstr);
                        if (adaptedInstr.ops.size() > 3)
                        {
                            adaptedInstr.ops.resize(3);
                        }
                        break;
                    }
                    case il::core::Opcode::Zext1:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "zext";
                        convertOperands(instr, {ILValue::Kind::I1}, adaptedInstr);
                        (void)kind;
                        break;
                    }
                    case il::core::Opcode::Trunc1:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "trunc";
                        convertOperands(instr, {ILValue::Kind::I64}, adaptedInstr);
                        (void)kind;
                        break;
                    }
                    case il::core::Opcode::CastSiToFp:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "sitofp";
                        convertOperands(instr, {ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::CastFpToSiRteChk:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "fptosi";
                        convertOperands(instr, {ILValue::Kind::F64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Br:
                    {
                        adaptedInstr.opcode = "br";
                        if (!instr.labels.empty())
                        {
                            adaptedInstr.ops.push_back(makeLabelValue(instr.labels.front()));
                        }
                        const std::size_t succCount = instr.labels.size();
                        adaptedBlock.terminatorEdges.reserve(adaptedBlock.terminatorEdges.size() +
                                                             succCount);
                        for (std::size_t idx = 0; idx < succCount; ++idx)
                        {
                            ILBlock::EdgeArg edge{};
                            edge.to = instr.labels[idx];
                            if (idx < instr.brArgs.size())
                            {
                                for (const auto &arg : instr.brArgs[idx])
                                {
                                    if (arg.kind != il::core::Value::Kind::Temp)
                                    {
                                        reportUnsupported(
                                            "non-SSA block argument in Phase A lowering");
                                    }
                                    edge.argIds.push_back(static_cast<int>(arg.id));
                                }
                            }
                            adaptedBlock.terminatorEdges.push_back(std::move(edge));
                        }
                        break;
                    }
                    case il::core::Opcode::CBr:
                    {
                        adaptedInstr.opcode = "cbr";
                        if (instr.operands.empty())
                        {
                            reportUnsupported("conditional branch missing condition operand");
                        }
                        adaptedInstr.ops.push_back(
                            convertValue(instr.operands.front(), ILValue::Kind::I1));
                        const std::size_t succCount = instr.labels.size();
                        for (std::size_t idx = 0; idx < succCount; ++idx)
                        {
                            adaptedInstr.ops.push_back(makeLabelValue(instr.labels[idx]));
                        }
                        adaptedBlock.terminatorEdges.reserve(adaptedBlock.terminatorEdges.size() +
                                                             succCount);
                        for (std::size_t idx = 0; idx < succCount; ++idx)
                        {
                            ILBlock::EdgeArg edge{};
                            edge.to = instr.labels[idx];
                            if (idx < instr.brArgs.size())
                            {
                                for (const auto &arg : instr.brArgs[idx])
                                {
                                    if (arg.kind != il::core::Value::Kind::Temp)
                                    {
                                        reportUnsupported(
                                            "non-SSA block argument in Phase A lowering");
                                    }
                                    edge.argIds.push_back(static_cast<int>(arg.id));
                                }
                            }
                            adaptedBlock.terminatorEdges.push_back(std::move(edge));
                        }
                        break;
                    }
                    case il::core::Opcode::Ret:
                    {
                        adaptedInstr.opcode = "ret";
                        if (!instr.operands.empty())
                        {
                            const auto returnKind =
                                func.retType.kind == il::core::Type::Kind::Void
                                    ? std::optional<ILValue::Kind>{}
                                    : std::optional<ILValue::Kind>{typeToKind(func.retType)};
                            adaptedInstr.ops.push_back(
                                convertValue(instr.operands.front(), returnKind));
                        }
                        break;
                    }
                    default:
                        reportUnsupported(std::string{"IL opcode '"} +
                                          il::core::toString(instr.op) +
                                          "' not supported by x86-64 Phase A");
                }

                adaptedBlock.instrs.push_back(std::move(adaptedInstr));
            }

            adaptedFunc.blocks.push_back(std::move(adaptedBlock));
        }

        adapted.funcs.push_back(std::move(adaptedFunc));
    }

    return adapted;
}

/// @brief Parse command-line flags into @p config.
std::optional<CodegenConfig> parseArgs(int argc, char **argv)
{
    if (argc < 1)
    {
        printUsageHint();
        return std::nullopt;
    }

    CodegenConfig config{};
    config.inputPath = argv[0];

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "-S")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "error: -S requires an output path\n";
                printUsageHint();
                return std::nullopt;
            }
            config.assemblyPath = argv[++i];
            continue;
        }
        if (arg == "-o")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "error: -o requires an output path\n";
                printUsageHint();
                return std::nullopt;
            }
            config.executablePath = argv[++i];
            continue;
        }
        if (arg == "-run-native")
        {
            config.runNative = true;
            continue;
        }

        std::cerr << "error: unknown flag '" << arg << "'\n";
        printUsageHint();
        return std::nullopt;
    }

    return config;
}

/// @brief Derive a default assembly output path when the user does not supply one.
std::filesystem::path deriveAssemblyPath(const CodegenConfig &config)
{
    std::filesystem::path assembly = std::filesystem::path(config.inputPath);
    if (assembly.empty())
    {
        return std::filesystem::path("out.s");
    }
    assembly.replace_extension(".s");
    if (assembly.filename().empty())
    {
        assembly = assembly.parent_path() / "out.s";
    }
    return assembly;
}

/// @brief Derive a default executable path when not explicitly requested.
std::filesystem::path deriveExecutablePath(const CodegenConfig &config)
{
    std::filesystem::path exe = std::filesystem::path(config.inputPath);
    if (exe.empty())
    {
        return std::filesystem::path("a.out");
    }
    exe.replace_extension("");
    if (exe.filename().empty() || exe.filename() == ".")
    {
        return exe.parent_path() / "a.out";
    }
    return exe;
}

/// @brief Write assembly text to disk, overwriting @p path on success.
bool writeAssemblyFile(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        std::cerr << "error: unable to open '" << path.string() << "' for writing\n";
        return false;
    }
    out << text;
    if (!out)
    {
        std::cerr << "error: failed to write assembly to '" << path.string() << "'\n";
        return false;
    }
    return true;
}

/// @brief Invoke the system C compiler to assemble and link the module.
int invokeLinker(const std::filesystem::path &asmPath, const std::filesystem::path &exePath)
{
    std::ostringstream cmd;
    cmd << "cc \"" << asmPath.string() << "\" -o \"" << exePath.string() << "\"";
    const std::string command = cmd.str();
    const int status =
        std::system(command.c_str()); // TODO: replace with process management that captures output.
    if (status == -1)
    {
        std::cerr << "error: failed to launch system linker command\n";
        return -1;
    }
    const int exitCode = normaliseSystemStatus(status);
    if (exitCode != 0)
    {
        std::cerr << "error: cc exited with status " << exitCode << "\n";
    }
    return exitCode;
}

/// @brief Execute the generated binary when requested.
int runExecutable(const std::filesystem::path &exePath)
{
    std::ostringstream cmd;
    cmd << "\"" << exePath.string() << "\"";
    const std::string command = cmd.str();
    const int status =
        std::system(command.c_str()); // TODO: replace with process management that captures output.
    if (status == -1)
    {
        std::cerr << "error: failed to execute '" << exePath.string() << "'\n";
        return -1;
    }
    return normaliseSystemStatus(status);
}

} // namespace

int cmd_codegen_x64(int argc, char **argv)
{
    auto configOpt = parseArgs(argc, argv);
    if (!configOpt)
    {
        return 1;
    }
    const CodegenConfig config = *configOpt;

    il::core::Module module;
    const auto loadResult =
        il::tools::common::loadModuleFromFile(config.inputPath, module, std::cerr);
    if (!loadResult.succeeded())
    {
        return 1;
    }
    if (!il::tools::common::verifyModule(module, std::cerr))
    {
        return 1;
    }

    const viper::codegen::x64::ILModule adapted = convertToAdapterModule(module);
    const viper::codegen::x64::CodegenResult result =
        viper::codegen::x64::emitModuleToAssembly(adapted, {});
    if (!result.errors.empty())
    {
        std::cerr << "error: x64 codegen failed:\n" << result.errors << "\n";
        return 1;
    }

    const std::filesystem::path asmPath = config.assemblyPath
                                              ? std::filesystem::path(*config.assemblyPath)
                                              : deriveAssemblyPath(config);
    if (!writeAssemblyFile(asmPath, result.asmText))
    {
        return 1;
    }

    const bool needLink = config.executablePath.has_value() || config.runNative;
    if (!needLink)
    {
        return 0;
    }

    const std::filesystem::path exePath = config.executablePath
                                              ? std::filesystem::path(*config.executablePath)
                                              : deriveExecutablePath(config);
    const int linkExit = invokeLinker(asmPath, exePath);
    if (linkExit != 0)
    {
        return linkExit == -1 ? 1 : linkExit;
    }

    if (!config.runNative)
    {
        return 0;
    }

    const int runExit = runExecutable(exePath);
    if (runExit == -1)
    {
        return 1;
    }
    return runExit;
}

void register_codegen_x64_commands(CLI &cli)
{
    (void)cli;
    // TODO: Integrate with the structured CLI once available.
}

} // namespace viper::tools::ilc
