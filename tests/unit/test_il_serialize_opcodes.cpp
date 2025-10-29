#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/io/Serializer.hpp"
#include <cassert>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

int main()
{
    using namespace il::core;

    Module m;

    Extern ext;
    ext.name = "do_work";
    ext.retType = Type(Type::Kind::I64);
    ext.params = {Type(Type::Kind::I64), Type(Type::Kind::I64)};
    m.externs.push_back(ext);

    Global g;
    g.name = ".Lstr";
    g.type = Type(Type::Kind::Str);
    g.init = "ops";
    m.globals.push_back(g);

    Function f;
    f.name = "all_ops";
    f.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    unsigned nextTemp = 0;

    auto emitValue = [&](Opcode op, Type type, std::initializer_list<Value> operands) -> Value
    {
        Instr instr;
        instr.result = nextTemp;
        instr.op = op;
        instr.type = type;
        instr.operands = std::vector<Value>(operands);
        entry.instructions.push_back(std::move(instr));
        return Value::temp(nextTemp++);
    };

    auto emitVoid = [&](Opcode op, Type type, std::initializer_list<Value> operands)
    {
        Instr instr;
        instr.op = op;
        instr.type = type;
        instr.operands = std::vector<Value>(operands);
        entry.instructions.push_back(std::move(instr));
    };

    auto emitCall =
        [&](const std::string &callee, Type type, std::initializer_list<Value> operands) -> Value
    {
        Instr instr;
        instr.result = nextTemp;
        instr.op = Opcode::Call;
        instr.type = type;
        instr.callee = callee;
        instr.operands = std::vector<Value>(operands);
        entry.instructions.push_back(std::move(instr));
        return Value::temp(nextTemp++);
    };

    Value addRes =
        emitValue(Opcode::IAddOvf, Type(Type::Kind::I64), {Value::constInt(1), Value::constInt(2)});
    Value subRes = emitValue(Opcode::ISubOvf, Type(Type::Kind::I64), {addRes, Value::constInt(3)});
    Value mulRes = emitValue(Opcode::IMulOvf, Type(Type::Kind::I64), {addRes, subRes});
    [[maybe_unused]] Value sdivRes =
        emitValue(Opcode::SDivChk0, Type(Type::Kind::I64), {mulRes, Value::constInt(5)});
    [[maybe_unused]] Value udivRes = emitValue(
        Opcode::UDivChk0, Type(Type::Kind::I64), {Value::constInt(10), Value::constInt(2)});
    [[maybe_unused]] Value sremRes = emitValue(
        Opcode::SRemChk0, Type(Type::Kind::I64), {Value::constInt(7), Value::constInt(3)});
    [[maybe_unused]] Value uremRes = emitValue(
        Opcode::URemChk0, Type(Type::Kind::I64), {Value::constInt(9), Value::constInt(4)});
    Value andRes = emitValue(
        Opcode::And, Type(Type::Kind::I64), {Value::constInt(0xF0), Value::constInt(0x0F)});
    Value orRes = emitValue(Opcode::Or, Type(Type::Kind::I64), {andRes, Value::constInt(1)});
    Value xorRes = emitValue(Opcode::Xor, Type(Type::Kind::I64), {orRes, Value::constInt(3)});
    Value shlRes = emitValue(Opcode::Shl, Type(Type::Kind::I64), {xorRes, Value::constInt(1)});
    [[maybe_unused]] Value lshrRes =
        emitValue(Opcode::LShr, Type(Type::Kind::I64), {shlRes, Value::constInt(2)});
    [[maybe_unused]] Value ashrRes =
        emitValue(Opcode::AShr, Type(Type::Kind::I64), {Value::constInt(-8), Value::constInt(1)});
    Value faddRes = emitValue(
        Opcode::FAdd, Type(Type::Kind::F64), {Value::constFloat(1.0), Value::constFloat(2.5)});
    Value fsubRes =
        emitValue(Opcode::FSub, Type(Type::Kind::F64), {faddRes, Value::constFloat(1.25)});
    Value fmulRes =
        emitValue(Opcode::FMul, Type(Type::Kind::F64), {fsubRes, Value::constFloat(4.0)});
    [[maybe_unused]] Value fdivRes =
        emitValue(Opcode::FDiv, Type(Type::Kind::F64), {fmulRes, Value::constFloat(2.0)});
    [[maybe_unused]] Value icmpEqRes =
        emitValue(Opcode::ICmpEq, Type(Type::Kind::I1), {Value::constInt(1), Value::constInt(1)});
    Value icmpNeRes =
        emitValue(Opcode::ICmpNe, Type(Type::Kind::I1), {Value::constInt(1), Value::constInt(0)});
    [[maybe_unused]] Value scmpLtRes =
        emitValue(Opcode::SCmpLT, Type(Type::Kind::I1), {Value::constInt(-1), Value::constInt(0)});
    [[maybe_unused]] Value scmpLeRes =
        emitValue(Opcode::SCmpLE, Type(Type::Kind::I1), {Value::constInt(0), Value::constInt(0)});
    [[maybe_unused]] Value scmpGtRes =
        emitValue(Opcode::SCmpGT, Type(Type::Kind::I1), {Value::constInt(2), Value::constInt(1)});
    [[maybe_unused]] Value scmpGeRes =
        emitValue(Opcode::SCmpGE, Type(Type::Kind::I1), {Value::constInt(2), Value::constInt(2)});
    [[maybe_unused]] Value ucmpLtRes =
        emitValue(Opcode::UCmpLT, Type(Type::Kind::I1), {Value::constInt(1), Value::constInt(2)});
    [[maybe_unused]] Value ucmpLeRes =
        emitValue(Opcode::UCmpLE, Type(Type::Kind::I1), {Value::constInt(2), Value::constInt(2)});
    [[maybe_unused]] Value ucmpGtRes =
        emitValue(Opcode::UCmpGT, Type(Type::Kind::I1), {Value::constInt(3), Value::constInt(2)});
    [[maybe_unused]] Value ucmpGeRes =
        emitValue(Opcode::UCmpGE, Type(Type::Kind::I1), {Value::constInt(3), Value::constInt(3)});
    [[maybe_unused]] Value fcmpEqRes = emitValue(
        Opcode::FCmpEQ, Type(Type::Kind::I1), {Value::constFloat(1.0), Value::constFloat(1.0)});
    [[maybe_unused]] Value fcmpNeRes = emitValue(
        Opcode::FCmpNE, Type(Type::Kind::I1), {Value::constFloat(1.0), Value::constFloat(2.0)});
    [[maybe_unused]] Value fcmpLtRes = emitValue(
        Opcode::FCmpLT, Type(Type::Kind::I1), {Value::constFloat(1.0), Value::constFloat(2.0)});
    [[maybe_unused]] Value fcmpLeRes = emitValue(
        Opcode::FCmpLE, Type(Type::Kind::I1), {Value::constFloat(2.0), Value::constFloat(2.0)});
    [[maybe_unused]] Value fcmpGtRes = emitValue(
        Opcode::FCmpGT, Type(Type::Kind::I1), {Value::constFloat(3.0), Value::constFloat(2.0)});
    [[maybe_unused]] Value fcmpGeRes = emitValue(
        Opcode::FCmpGE, Type(Type::Kind::I1), {Value::constFloat(3.0), Value::constFloat(3.0)});
    [[maybe_unused]] Value sitofpRes =
        emitValue(Opcode::Sitofp, Type(Type::Kind::F64), {Value::constInt(42)});
    [[maybe_unused]] Value fptosiRes =
        emitValue(Opcode::Fptosi, Type(Type::Kind::I64), {Value::constFloat(5.5)});
    [[maybe_unused]] Value castFpToSiChkRes =
        emitValue(Opcode::CastFpToSiRteChk, Type(Type::Kind::I64), {Value::constFloat(5.5)});
    Value zextRes = emitValue(Opcode::Zext1, Type(Type::Kind::I64), {icmpNeRes});
    [[maybe_unused]] Value truncRes =
        emitValue(Opcode::Trunc1, Type(Type::Kind::I1), {Value::constInt(255)});
    Value allocaRes = emitValue(Opcode::Alloca, Type(Type::Kind::Ptr), {Value::constInt(8)});
    [[maybe_unused]] Value gepRes =
        emitValue(Opcode::GEP, Type(Type::Kind::Ptr), {allocaRes, Value::constInt(1)});
    emitVoid(Opcode::Store, Type(Type::Kind::I64), {allocaRes, Value::constInt(64)});
    Value loadRes = emitValue(Opcode::Load, Type(Type::Kind::I64), {allocaRes});
    Value addrOfRes = emitValue(Opcode::AddrOf, Type(Type::Kind::Ptr), {Value::global(g.name)});
    [[maybe_unused]] Value constStrRes =
        emitValue(Opcode::ConstStr, Type(Type::Kind::Str), {Value::global(g.name)});
    Value constNullRes = emitValue(Opcode::ConstNull, Type(Type::Kind::Ptr), {});
    Value callRes = emitCall(ext.name, Type(Type::Kind::I64), {addrOfRes, Value::constInt(5)});

    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(icmpNeRes);
    cbr.labels = {"compute", "abort"};
    cbr.brArgs = {{zextRes, loadRes}, {constNullRes}};
    entry.instructions.push_back(std::move(cbr));
    entry.terminated = true;

    BasicBlock compute;
    compute.label = "compute";
    compute.params.push_back(Param{"wide", Type(Type::Kind::I64), 0});
    compute.params.push_back(Param{"loaded", Type(Type::Kind::I64), 1});
    Instr br;
    br.op = Opcode::Br;
    br.type = Type(Type::Kind::Void);
    br.labels = {"join"};
    br.brArgs = {{callRes, zextRes}};
    compute.instructions.push_back(std::move(br));
    compute.terminated = true;

    BasicBlock abortBlock;
    abortBlock.label = "abort";
    Instr trap;
    trap.op = Opcode::Trap;
    trap.type = Type(Type::Kind::Void);
    abortBlock.instructions.push_back(std::move(trap));
    abortBlock.terminated = true;

    BasicBlock join;
    join.label = "join";
    join.params.push_back(Param{"lhs", Type(Type::Kind::I64), 0});
    join.params.push_back(Param{"rhs", Type(Type::Kind::I64), 1});
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(callRes);
    join.instructions.push_back(std::move(ret));
    join.terminated = true;

    f.blocks.push_back(std::move(entry));
    f.blocks.push_back(std::move(compute));
    f.blocks.push_back(std::move(abortBlock));
    f.blocks.push_back(std::move(join));
    m.functions.push_back(std::move(f));

    std::string out = il::io::Serializer::toString(m);
    std::ifstream in(std::string(TESTS_DIR) + "/golden/il/serializer_all_opcodes.il");
    std::stringstream buf;
    buf << in.rdbuf();
    std::string expected = buf.str();
    if (!out.empty() && out.back() == '\n')
        out.pop_back();
    if (!expected.empty() && expected.back() == '\n')
        expected.pop_back();
    assert(out == expected);
    return 0;
}
