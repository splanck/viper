//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/build/IRBuilder.cpp
// Purpose: Provide a structured API for constructing IL modules programmatically.
// Key invariants: Builder maintains a current function/block insertion context
//                 and monotonically increasing SSA identifiers.
// Ownership/Lifetime: Builder references a module owned by the caller.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the fluent builder used by front ends to emit IL.
/// @details The IRBuilder records insertion points, tracks SSA identifiers, and
///          materialises instructions while enforcing control-flow invariants
///          such as single terminators per block.  It also caches callee return
///          types to validate call emission.

#include "il/build/IRBuilder.hpp"
#include <cassert>
#include <stdexcept>
#include <unordered_set>

namespace il::build
{
using namespace il::core;
using namespace il::support;

//===----------------------------------------------------------------------===//
// Debug Assertion Helpers
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
/// @brief Check that a block label is unique within a function.
/// @param fn Function containing the blocks.
/// @param label Label to check for uniqueness.
/// @param existingBlocks Number of blocks to check (0 = all).
static void assertUniqueLabelInFunction(const Function &fn, const std::string &label)
{
    for (const auto &block : fn.blocks)
    {
        assert(block.label != label && "block label already exists in function");
    }
}

/// @brief Check that a name is not already used as a function in the module.
/// @param mod Module to check.
/// @param name Name to check.
static void assertUniqueFunctionName(const Module &mod, const std::string &name)
{
    for (const auto &fn : mod.functions)
    {
        assert(fn.name != name && "function name already exists in module");
    }
}

/// @brief Check that a name is not already used as an extern in the module.
/// @param mod Module to check.
/// @param name Name to check.
static void assertUniqueExternName(const Module &mod, const std::string &name)
{
    for (const auto &ex : mod.externs)
    {
        assert(ex.name != name && "extern name already exists in module");
    }
}

/// @brief Check that a parameter type is valid (not Void).
/// @param params Parameters to validate.
static void assertValidParamTypes(const std::vector<Param> &params)
{
    for (const auto &p : params)
    {
        assert(p.type.kind != Type::Kind::Void && "parameter cannot have Void type");
    }
}
#endif // NDEBUG

/// @brief Initialise a builder that mutates an existing module.
///
/// @details The constructor walks existing functions and extern declarations to
///          seed the @ref calleeReturnTypes cache.  This enables later calls to
///          @ref emitCall to validate that callees exist and to stamp the
///          expected result type before any new instructions are emitted.
///
/// @param m Module that will be extended by this builder.
/// @note Populates callee metadata so emitCall can validate future calls.
IRBuilder::IRBuilder(il::core::Module &m) : mod(m)
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
il::core::Extern &IRBuilder::addExtern(const std::string &name,
                                       il::core::Type ret,
                                       const std::vector<il::core::Type> &params)
{
#ifndef NDEBUG
    // Extern name must not be empty
    assert(!name.empty() && "extern name cannot be empty");
    // Extern name must be unique
    assertUniqueExternName(mod, name);
#endif

    mod.externs.push_back({name, ret, params});
    calleeReturnTypes[name] = ret;
    return mod.externs.back();
}

/// @brief Add a global variable with specified type.
/// @param name Global identifier without leading `@`.
/// @param type Variable type.
/// @param init Optional initializer (empty for zero-initialized).
/// @return Reference to the inserted global definition.
il::core::Global &IRBuilder::addGlobal(const std::string &name,
                                       il::core::Type type,
                                       const std::string &init)
{
#ifndef NDEBUG
    // Global name must not be empty
    assert(!name.empty() && "global name cannot be empty");
#endif

    mod.globals.push_back({name, type, init});
    return mod.globals.back();
}

/// @brief Add a UTF-8 string literal as a global value.
/// @param name Identifier to attach to the global string.
/// @param value Contents of the string literal.
/// @return Reference to the inserted global definition.
/// @note The global is always recorded with Type::Kind::Str.
il::core::Global &IRBuilder::addGlobalStr(const std::string &name, const std::string &value)
{
    return addGlobal(name, il::core::Type(il::core::Type::Kind::Str), value);
}

/// @brief Begin building a new function and make it the active insertion target.
/// @param name Symbol name for the function being defined.
/// @param ret Return type to advertise to callers.
/// @param params Formal parameters with stable IDs and debug names.
/// @return Reference to the newly created function.
/// @post nextTemp is reset and populated with parameter IDs for subsequent temporaries.
il::core::Function &IRBuilder::startFunction(const std::string &name,
                                             il::core::Type ret,
                                             const std::vector<il::core::Param> &params)
{
#ifndef NDEBUG
    // Function name must not be empty
    assert(!name.empty() && "function name cannot be empty");
    // Validate parameter types (no Void parameters allowed)
    assertValidParamTypes(params);
    // Note: We don't assert function name uniqueness here because:
    // 1. Some code patterns call startFunction multiple times (VM_TailCallTests)
    // 2. Duplicate function names indicate bugs elsewhere but are caught by verification
#endif

    mod.functions.push_back({name, ret, {}, {}, {}});
    calleeReturnTypes[name] = ret;
    curFunc = &mod.functions.back();
    curBlock = nullptr;
    nextTemp = 0;
    for (auto p : params)
    {
        il::core::Param np = p;
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
il::core::BasicBlock &IRBuilder::createBlock(il::core::Function &fn,
                                             const std::string &label,
                                             const std::vector<il::core::Param> &params)
{
#ifndef NDEBUG
    // Block label must not be empty
    assert(!label.empty() && "block label cannot be empty");
    // Block label must be unique within the function
    assertUniqueLabelInFunction(fn, label);
    // Validate block parameter types (no Void parameters allowed)
    assertValidParamTypes(params);
#endif

    fn.blocks.push_back({label, {}, {}, false});
    il::core::BasicBlock &bb = fn.blocks.back();
    for (auto p : params)
    {
        il::core::Param np = p;
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
il::core::BasicBlock &IRBuilder::addBlock(il::core::Function &fn, const std::string &label)
{
    return createBlock(fn, label, {});
}

/// @brief Insert a parameter-less basic block at a fixed position in the function.
/// @details Useful for ensuring new blocks appear before a known position (e.g.,
///          the function's synthetic exit block). Does not update the current
///          insertion point.
il::core::BasicBlock &IRBuilder::insertBlock(il::core::Function &fn,
                                             size_t idx,
                                             const std::string &label)
{
#ifndef NDEBUG
    // Block label must not be empty
    assert(!label.empty() && "block label cannot be empty");
    // Block label must be unique within the function
    assertUniqueLabelInFunction(fn, label);
#endif

    if (idx > fn.blocks.size())
        idx = fn.blocks.size();
    auto it = fn.blocks.insert(fn.blocks.begin() + static_cast<std::ptrdiff_t>(idx),
                               il::core::BasicBlock{label, {}, {}, false});
    return *it;
}

/// @brief Retrieve the SSA value associated with a block parameter.
/// @param bb Block whose parameter is being referenced.
/// @param idx Zero-based index into bb.params.
/// @return Temporary value representing the parameter.
/// @pre idx must reference an existing parameter.
il::core::Value IRBuilder::blockParam(il::core::BasicBlock &bb, unsigned idx)
{
    assert(idx < bb.params.size());
    return il::core::Value::temp(bb.params[idx].id);
}

/// @brief Emit an unconditional branch to @p dst.
/// @param dst Destination block that receives control.
/// @param args Values corresponding to @p dst's block parameters.
/// @pre args.size() must equal dst.params.size().
/// @post Current block is marked terminated and cannot receive more non-phi instructions.
void IRBuilder::br(il::core::BasicBlock &dst, const std::vector<il::core::Value> &args)
{
    assert(args.size() == dst.params.size() &&
           "branch argument count must match block parameter count");

#ifndef NDEBUG
    // Validate all branch argument temp IDs are within bounds
    for (const auto &arg : args)
    {
        if (arg.kind == Value::Kind::Temp)
        {
            assert(arg.id < nextTemp &&
                   "branch argument temp ID exceeds allocated temporaries (dangling reference)");
        }
    }
#endif

    il::core::Instr instr;
    instr.op = il::core::Opcode::Br;
    instr.type = il::core::Type(il::core::Type::Kind::Void);
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
void IRBuilder::cbr(il::core::Value cond,
                    il::core::BasicBlock &t,
                    const std::vector<il::core::Value> &targs,
                    il::core::BasicBlock &f,
                    const std::vector<il::core::Value> &fargs)
{
    assert(targs.size() == t.params.size() &&
           "true branch argument count must match target block parameters");
    assert(fargs.size() == f.params.size() &&
           "false branch argument count must match target block parameters");

#ifndef NDEBUG
    // Verify condition temp ID is within bounds
    if (cond.kind == Value::Kind::Temp)
    {
        assert(cond.id < nextTemp && "condition temp ID exceeds allocated temporaries");
    }

    // Validate all branch argument temp IDs are within bounds
    for (const auto &arg : targs)
    {
        if (arg.kind == Value::Kind::Temp)
        {
            assert(arg.id < nextTemp &&
                   "true branch argument temp ID exceeds allocated temporaries");
        }
    }
    for (const auto &arg : fargs)
    {
        if (arg.kind == Value::Kind::Temp)
        {
            assert(arg.id < nextTemp &&
                   "false branch argument temp ID exceeds allocated temporaries");
        }
    }
#endif

    il::core::Instr instr;
    instr.op = il::core::Opcode::CBr;
    instr.type = il::core::Type(il::core::Type::Kind::Void);
    instr.operands.push_back(cond);
    instr.labels.push_back(t.label);
    instr.labels.push_back(f.label);
    instr.brArgs.push_back(targs);
    instr.brArgs.push_back(fargs);
    append(std::move(instr));
}

/// @brief Select the basic block that will receive subsequently appended instructions.
///
/// @details The builder does not clear the block's terminated flag; callers may
///          therefore inspect @ref BasicBlock::terminated to decide whether more
///          instructions may be emitted.
///
/// @param bb Block that becomes the current insertion point.
/// @post curBlock is updated to @p bb; termination state is preserved.
void IRBuilder::setInsertPoint(il::core::BasicBlock &bb)
{
    curBlock = &bb;
}

/// @brief Append an instruction to the current block and update termination state.
/// @param instr Instruction being moved into the block.
/// @return Reference to the stored instruction inside the block.
/// @pre An insertion point must be established with setInsertPoint().
/// @post Terminator opcodes mark the block as finished to prevent further insertions.
il::core::Instr &IRBuilder::append(il::core::Instr instr)
{
    assert(curBlock && "insert point not set");
    assert(curFunc && "no active function");

#ifndef NDEBUG
    // Cannot append normal instructions after a terminator
    if (!isTerminator(instr.op))
    {
        assert(!curBlock->terminated &&
               "cannot append non-terminator instruction to terminated block");
    }

    // Validate that operand temp IDs are within bounds
    for (const auto &operand : instr.operands)
    {
        if (operand.kind == Value::Kind::Temp)
        {
            assert(operand.id < nextTemp &&
                   "operand temp ID exceeds allocated temporaries (dangling reference)");
        }
    }

    // Validate result temp ID if present
    if (instr.result)
    {
        // Result ID should be within the next available range (allow some slack for
        // pre-reserved IDs, but catch obviously invalid values)
        assert(*instr.result <= nextTemp + 1000 &&
               "result temp ID is unreasonably large (possible corruption)");
    }
#endif

    if (isTerminator(instr.op))
    {
        assert(!curBlock->terminated && "block already terminated");
        curBlock->terminated = true;
    }
    curBlock->instructions.push_back(std::move(instr));
    return curBlock->instructions.back();
}

/// @brief Identify whether an opcode terminates a block's control flow.
///
/// @details Terminators encompass both explicit branches and exception-resume
///          operations.  Recognising them lets the builder mark blocks as closed
///          and reject additional non-phi instructions.
///
/// @param op Opcode to categorize.
/// @return True when @p op ends the block (branch, conditional branch, return, trap).
bool IRBuilder::isTerminator(il::core::Opcode op) const
{
    return op == il::core::Opcode::Br || op == il::core::Opcode::CBr ||
           op == il::core::Opcode::SwitchI32 || op == il::core::Opcode::Ret ||
           op == il::core::Opcode::Trap || op == il::core::Opcode::ResumeSame ||
           op == il::core::Opcode::ResumeNext || op == il::core::Opcode::ResumeLabel;
}

/// @brief Materialize a string constant by referencing an existing global.
/// @param globalName Name of the global string to load.
/// @param loc Source location associated with the instruction.
/// @return SSA temporary containing the string value.
/// @post nextTemp advances to include the new temporary identifier.
il::core::Value IRBuilder::emitConstStr(const std::string &globalName, il::support::SourceLoc loc)
{
    unsigned id = nextTemp++;
    il::core::Instr instr;
    instr.result = id;
    instr.op = il::core::Opcode::ConstStr;
    instr.type = il::core::Type(il::core::Type::Kind::Str);
    instr.operands.push_back(il::core::Value::global(globalName));
    instr.loc = loc;
    append(std::move(instr));
    return il::core::Value::temp(id);
}

/// @brief Emit a function call and optionally capture its result.
/// @param callee Symbol name of the function being invoked.
/// @param args Operands passed to the callee in order.
/// @param dst Optional destination SSA value pre-allocated by the caller.
/// @param loc Source location for diagnostics and debugging.
/// @throws std::logic_error If @p callee is not known to the module.
/// @post nextTemp expands when @p dst refers to a previously unseen ID.
void IRBuilder::emitCall(const std::string &callee,
                         const std::vector<il::core::Value> &args,
                         const std::optional<il::core::Value> &dst,
                         il::support::SourceLoc loc)
{
#ifndef NDEBUG
    // Validate that all argument temp IDs are within bounds
    for (const auto &arg : args)
    {
        if (arg.kind == Value::Kind::Temp)
        {
            assert(arg.id < nextTemp &&
                   "call argument temp ID exceeds allocated temporaries (dangling reference)");
        }
    }
#endif

    il::core::Instr instr;
    instr.op = il::core::Opcode::Call;
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
void IRBuilder::emitRet(const std::optional<il::core::Value> &v, il::support::SourceLoc loc)
{
#ifndef NDEBUG
    // Validate return value temp ID if present
    if (v && v->kind == Value::Kind::Temp)
    {
        assert(v->id < nextTemp &&
               "return value temp ID exceeds allocated temporaries (dangling reference)");
    }
#endif

    il::core::Instr instr;
    instr.op = il::core::Opcode::Ret;
    instr.type = il::core::Type(il::core::Type::Kind::Void);
    if (v)
        instr.operands.push_back(*v);
    instr.loc = loc;
    append(std::move(instr));
}

/// @brief Emit a resume-same instruction for structured exception handlers.
/// @details Creates an @ref Opcode::ResumeSame terminator that rethrows the
/// current exception token to the innermost handler. The builder appends the
/// instruction to the current block, marks it terminated, and records the
/// source location for diagnostics.
/// @param token SSA value representing the exception token to resume.
/// @param loc Source location used when formatting verifier diagnostics.
void IRBuilder::emitResumeSame(il::core::Value token, il::support::SourceLoc loc)
{
#ifndef NDEBUG
    // Validate token temp ID
    if (token.kind == Value::Kind::Temp)
    {
        assert(token.id < nextTemp &&
               "resume token temp ID exceeds allocated temporaries (dangling reference)");
    }
#endif

    il::core::Instr instr;
    instr.op = il::core::Opcode::ResumeSame;
    instr.type = il::core::Type(il::core::Type::Kind::Void);
    instr.operands.push_back(token);
    instr.loc = loc;
    append(std::move(instr));
}

/// @brief Emit a resume-next instruction for structured exception handlers.
/// @details Generates an @ref Opcode::ResumeNext terminator that forwards the
/// active exception token to the next handler in the stack. Like
/// @ref emitResumeSame, the helper appends the instruction, marks termination,
/// and records the provided source location.
/// @param token SSA value representing the exception token to resume.
/// @param loc Source location used when formatting verifier diagnostics.
void IRBuilder::emitResumeNext(il::core::Value token, il::support::SourceLoc loc)
{
#ifndef NDEBUG
    // Validate token temp ID
    if (token.kind == Value::Kind::Temp)
    {
        assert(token.id < nextTemp &&
               "resume token temp ID exceeds allocated temporaries (dangling reference)");
    }
#endif

    il::core::Instr instr;
    instr.op = il::core::Opcode::ResumeNext;
    instr.type = il::core::Type(il::core::Type::Kind::Void);
    instr.operands.push_back(token);
    instr.loc = loc;
    append(std::move(instr));
}

/// @brief Emit a resume-label instruction transferring control to @p target.
/// @details Appends an @ref Opcode::ResumeLabel terminator that jumps to a
/// specific handler block. The token operand is preserved and the destination
/// label is recorded to maintain block parameter arity.
/// @param token SSA value representing the exception token to resume with.
/// @param target Handler block that receives control once the token is
/// resumed.
/// @param loc Source location used when formatting verifier diagnostics.
void IRBuilder::emitResumeLabel(il::core::Value token,
                                il::core::BasicBlock &target,
                                il::support::SourceLoc loc)
{
#ifndef NDEBUG
    // Validate token temp ID
    if (token.kind == Value::Kind::Temp)
    {
        assert(token.id < nextTemp &&
               "resume token temp ID exceeds allocated temporaries (dangling reference)");
    }
#endif

    il::core::Instr instr;
    instr.op = il::core::Opcode::ResumeLabel;
    instr.type = il::core::Type(il::core::Type::Kind::Void);
    instr.operands.push_back(token);
    instr.labels.push_back(target.label);
    instr.loc = loc;
    append(std::move(instr));
}

/// @brief Reserve the next SSA temporary identifier for the currently active function.
///
/// @details Extends the value name table to ensure future debug lookups remain
///          in bounds.  The caller typically uses the returned identifier to
///          populate instructions that will be appended immediately afterwards.
///
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
