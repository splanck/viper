// File: src/il/build/IRBuilder.cpp
// Purpose: Implements helpers to construct IL modules.
// Key invariants: None.
// Ownership/Lifetime: Builder references module owned externally.
// License: MIT (see LICENSE).
// Links: docs/il-guide.md#reference

#include "il/build/IRBuilder.hpp"
#include <cassert>
#include <stdexcept>

namespace il::build
{

/// @brief Initialize a builder that mutates an existing module.
/// @param m Module that will be extended by this builder.
/// @note Populates callee metadata so emitCall can validate future calls.
IRBuilder::IRBuilder(Module &m) : mod(m)
{
    for (const auto &fn : mod.functions)
        calleeReturnTypes[fn.name] = fn.retType;
    for (const auto &ex : mod.externs)
        calleeReturnTypes[ex.name] = ex.retType;
}

/// @brief Declare an external function and record its signature.
/// @param name Symbol name for the external function.
/// @param ret Return type advertised to callers.
/// @param params Ordered parameter types accepted by the callee.
/// @return Reference to the inserted extern declaration.
/// @post calleeReturnTypes is updated to match @p ret.
Extern &IRBuilder::addExtern(const std::string &name, Type ret, const std::vector<Type> &params)
{
    mod.externs.push_back({name, ret, params});
    calleeReturnTypes[name] = ret;
    return mod.externs.back();
}

/// @brief Add a UTF-8 string literal as a global value.
/// @param name Identifier to attach to the global string.
/// @param value Contents of the string literal.
/// @return Reference to the inserted global definition.
/// @note The global is always recorded with Type::Kind::Str.
Global &IRBuilder::addGlobalStr(const std::string &name, const std::string &value)
{
    mod.globals.push_back({name, Type(Type::Kind::Str), value});
    return mod.globals.back();
}

/// @brief Begin building a new function and make it the active insertion target.
/// @param name Symbol name for the function being defined.
/// @param ret Return type to advertise to callers.
/// @param params Formal parameters with stable IDs and debug names.
/// @return Reference to the newly created function.
/// @post nextTemp is reset and populated with parameter IDs for subsequent temporaries.
Function &IRBuilder::startFunction(const std::string &name,
                                   Type ret,
                                   const std::vector<Param> &params)
{
    mod.functions.push_back({name, ret, {}, {}, {}});
    calleeReturnTypes[name] = ret;
    curFunc = &mod.functions.back();
    curBlock = nullptr;
    nextTemp = 0;
    for (auto p : params)
    {
        Param np = p;
        np.id = nextTemp++;
        curFunc->params.push_back(np);
    }
    curFunc->valueNames.resize(nextTemp);
    for (const auto &p : curFunc->params)
        curFunc->valueNames[p.id] = p.name;
    return *curFunc;
}

/// @brief Create a basic block in @p fn with optional block parameters.
/// @param fn Function that owns the block; typically the function returned by startFunction().
/// @param label Unique label used when referencing the block.
/// @param params Block parameters that must align with incoming branch arguments.
/// @return Reference to the created block.
/// @post fn.valueNames is grown to include every parameter ID produced.
BasicBlock &IRBuilder::createBlock(Function &fn,
                                   const std::string &label,
                                   const std::vector<Param> &params)
{
    fn.blocks.push_back({label, {}, {}, false});
    BasicBlock &bb = fn.blocks.back();
    for (auto p : params)
    {
        Param np = p;
        np.id = nextTemp++;
        bb.params.push_back(np);
        if (fn.valueNames.size() <= np.id)
            fn.valueNames.resize(np.id + 1);
        fn.valueNames[np.id] = np.name;
    }
    return bb;
}

/// @brief Convenience wrapper for creating a block without parameters.
/// @param fn Function to receive the block.
/// @param label Label to assign to the block.
/// @return Reference to the created block.
BasicBlock &IRBuilder::addBlock(Function &fn, const std::string &label)
{
    return createBlock(fn, label, {});
}

/// @brief Retrieve the SSA value associated with a block parameter.
/// @param bb Block whose parameter is being referenced.
/// @param idx Zero-based index into bb.params.
/// @return Temporary value representing the parameter.
/// @pre idx must reference an existing parameter.
Value IRBuilder::blockParam(BasicBlock &bb, unsigned idx)
{
    assert(idx < bb.params.size());
    return Value::temp(bb.params[idx].id);
}

/// @brief Emit an unconditional branch to @p dst.
/// @param dst Destination block that receives control.
/// @param args Values corresponding to @p dst's block parameters.
/// @pre args.size() must equal dst.params.size().
/// @post Current block is marked terminated and cannot receive more non-phi instructions.
void IRBuilder::br(BasicBlock &dst, const std::vector<Value> &args)
{
    assert(args.size() == dst.params.size());
    Instr instr;
    instr.op = Opcode::Br;
    instr.type = Type(Type::Kind::Void);
    instr.labels.push_back(dst.label);
    instr.brArgs.push_back(args);
    append(std::move(instr));
}

/// @brief Emit a conditional branch with separate successor arguments.
/// @param cond SSA value determining which successor executes.
/// @param t Target block for the true edge.
/// @param targs Arguments supplied when branching to @p t.
/// @param f Target block for the false edge.
/// @param fargs Arguments supplied when branching to @p f.
/// @pre Argument counts must match the parameter lists of both targets.
/// @post Current block is marked terminated.
void IRBuilder::cbr(Value cond,
                    BasicBlock &t,
                    const std::vector<Value> &targs,
                    BasicBlock &f,
                    const std::vector<Value> &fargs)
{
    assert(targs.size() == t.params.size());
    assert(fargs.size() == f.params.size());
    Instr instr;
    instr.op = Opcode::CBr;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(cond);
    instr.labels.push_back(t.label);
    instr.labels.push_back(f.label);
    instr.brArgs.push_back(targs);
    instr.brArgs.push_back(fargs);
    append(std::move(instr));
}

/// @brief Select the basic block that will receive subsequently appended instructions.
/// @param bb Block that becomes the current insertion point.
/// @post curBlock is updated to @p bb; termination state is preserved.
void IRBuilder::setInsertPoint(BasicBlock &bb)
{
    curBlock = &bb;
}

/// @brief Append an instruction to the current block and update termination state.
/// @param instr Instruction being moved into the block.
/// @return Reference to the stored instruction inside the block.
/// @pre An insertion point must be established with setInsertPoint().
/// @post Terminator opcodes mark the block as finished to prevent further insertions.
Instr &IRBuilder::append(Instr instr)
{
    assert(curBlock && "insert point not set");
    if (isTerminator(instr.op))
    {
        assert(!curBlock->terminated && "block already terminated");
        curBlock->terminated = true;
    }
    curBlock->instructions.push_back(std::move(instr));
    return curBlock->instructions.back();
}

/// @brief Identify whether an opcode terminates a block's control flow.
/// @param op Opcode to categorize.
/// @return True when @p op ends the block (branch, conditional branch, return, trap).
bool IRBuilder::isTerminator(Opcode op) const
{
    return op == Opcode::Br || op == Opcode::CBr || op == Opcode::SwitchI32 || op == Opcode::Ret ||
           op == Opcode::Trap || op == Opcode::ResumeSame || op == Opcode::ResumeNext ||
           op == Opcode::ResumeLabel;
}

/// @brief Materialize a string constant by referencing an existing global.
/// @param globalName Name of the global string to load.
/// @param loc Source location associated with the instruction.
/// @return SSA temporary containing the string value.
/// @post nextTemp advances to include the new temporary identifier.
Value IRBuilder::emitConstStr(const std::string &globalName, il::support::SourceLoc loc)
{
    unsigned id = nextTemp++;
    Instr instr;
    instr.result = id;
    instr.op = Opcode::ConstStr;
    instr.type = Type(Type::Kind::Str);
    instr.operands.push_back(Value::global(globalName));
    instr.loc = loc;
    append(std::move(instr));
    return Value::temp(id);
}

/// @brief Emit a function call and optionally capture its result.
/// @param callee Symbol name of the function being invoked.
/// @param args Operands passed to the callee in order.
/// @param dst Optional destination SSA value pre-allocated by the caller.
/// @param loc Source location for diagnostics and debugging.
/// @throws std::logic_error If @p callee is not known to the module.
/// @post nextTemp expands when @p dst refers to a previously unseen ID.
void IRBuilder::emitCall(const std::string &callee,
                         const std::vector<Value> &args,
                         const std::optional<Value> &dst,
                         il::support::SourceLoc loc)
{
    Instr instr;
    instr.op = Opcode::Call;
    const auto it = calleeReturnTypes.find(callee);
    if (it == calleeReturnTypes.end())
        throw std::logic_error("emitCall: unknown callee '" + callee + "'");
    instr.type = it->second;
    instr.callee = callee;
    instr.operands = args;
    if (dst)
    {
        instr.result = dst->id;
        if (dst->id >= nextTemp)
        {
            nextTemp = dst->id + 1;
            if (curFunc->valueNames.size() <= dst->id)
                curFunc->valueNames.resize(dst->id + 1);
        }
    }
    instr.loc = loc;
    append(std::move(instr));
}

/// @brief Emit a return from the current function.
/// @param v Optional SSA value to return.
/// @param loc Source location carried by the instruction.
/// @post Marks the block as terminated and enforces the void return opcode when absent.
void IRBuilder::emitRet(const std::optional<Value> &v, il::support::SourceLoc loc)
{
    Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    if (v)
        instr.operands.push_back(*v);
    instr.loc = loc;
    append(std::move(instr));
}

void IRBuilder::emitResumeSame(Value token, il::support::SourceLoc loc)
{
    Instr instr;
    instr.op = Opcode::ResumeSame;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(token);
    instr.loc = loc;
    append(std::move(instr));
}

void IRBuilder::emitResumeNext(Value token, il::support::SourceLoc loc)
{
    Instr instr;
    instr.op = Opcode::ResumeNext;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(token);
    instr.loc = loc;
    append(std::move(instr));
}

void IRBuilder::emitResumeLabel(Value token, BasicBlock &target, il::support::SourceLoc loc)
{
    Instr instr;
    instr.op = Opcode::ResumeLabel;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(token);
    instr.labels.push_back(target.label);
    instr.loc = loc;
    append(std::move(instr));
}

/// @brief Reserve the next SSA temporary identifier for the currently active function.
/// @return Identifier assigned to the new temporary.
unsigned IRBuilder::reserveTempId()
{
    assert(curFunc && "reserveTempId requires an active function");
    unsigned id = nextTemp++;
    if (curFunc->valueNames.size() <= id)
        curFunc->valueNames.resize(id + 1);
    return id;
}

} // namespace il::build
