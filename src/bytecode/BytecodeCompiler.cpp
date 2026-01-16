// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.

#include "bytecode/BytecodeCompiler.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include <algorithm>
#include <cassert>
#include <unordered_set>

namespace viper {
namespace bytecode {

BytecodeModule BytecodeCompiler::compile(const il::core::Module& ilModule) {
    module_ = BytecodeModule();

    // Pre-register all function names to support recursive and forward calls
    for (size_t i = 0; i < ilModule.functions.size(); ++i) {
        module_.functionIndex[ilModule.functions[i].name] = static_cast<uint32_t>(i);
    }

    // Compile each function
    for (const auto& fn : ilModule.functions) {
        compileFunction(fn);
    }

    return std::move(module_);
}

void BytecodeCompiler::compileFunction(const il::core::Function& fn) {
    // Create new bytecode function
    BytecodeFunction bcFunc;
    bcFunc.name = fn.name;
    bcFunc.numParams = static_cast<uint32_t>(fn.params.size());
    bcFunc.hasReturn = fn.retType.kind != il::core::Type::Kind::Void;

    // Set as current function
    currentFunc_ = &bcFunc;

    // Reset compilation state
    ssaToLocal_.clear();
    blockOffsets_.clear();
    pendingBranches_.clear();
    currentStackDepth_ = 0;
    maxStackDepth_ = 0;

    // Build SSA to locals mapping
    buildSSAToLocalsMap(fn);
    bcFunc.numLocals = nextLocal_;

    // Linearize blocks
    auto blocks = linearizeBlocks(fn);

    // Compile each block
    for (const auto* block : blocks) {
        compileBlock(*block);
    }

    // Resolve branch offsets
    resolveBranches();

    // Record max stack depth
    bcFunc.maxStack = static_cast<uint32_t>(maxStackDepth_);

    // Add function to module
    module_.addFunction(std::move(bcFunc));
    currentFunc_ = nullptr;
}

void BytecodeCompiler::buildSSAToLocalsMap(const il::core::Function& fn) {
    nextLocal_ = 0;

    // Map parameters first (preserve order)
    for (const auto& param : fn.params) {
        ssaToLocal_[param.id] = nextLocal_++;
    }

    // Map block parameters
    for (const auto& block : fn.blocks) {
        for (const auto& param : block.params) {
            if (ssaToLocal_.find(param.id) == ssaToLocal_.end()) {
                ssaToLocal_[param.id] = nextLocal_++;
            }
        }
    }

    // Map instruction results
    for (const auto& block : fn.blocks) {
        for (const auto& instr : block.instructions) {
            if (instr.result) {
                if (ssaToLocal_.find(*instr.result) == ssaToLocal_.end()) {
                    ssaToLocal_[*instr.result] = nextLocal_++;
                }
            }
        }
    }
}

std::vector<const il::core::BasicBlock*> BytecodeCompiler::linearizeBlocks(
    const il::core::Function& fn) {
    // Simple depth-first ordering
    std::vector<const il::core::BasicBlock*> result;
    std::unordered_set<const il::core::BasicBlock*> visited;
    std::unordered_map<std::string, const il::core::BasicBlock*> labelToBlock;

    // Build label -> block mapping
    for (const auto& block : fn.blocks) {
        labelToBlock[block.label] = &block;
    }

    // DFS from entry block
    if (!fn.blocks.empty()) {
        std::vector<const il::core::BasicBlock*> worklist;
        worklist.push_back(&fn.blocks.front());

        while (!worklist.empty()) {
            const auto* block = worklist.back();
            worklist.pop_back();

            if (visited.count(block)) continue;
            visited.insert(block);
            result.push_back(block);

            // Add successor blocks (in reverse order for DFS)
            if (!block->instructions.empty()) {
                const auto& terminator = block->instructions.back();
                for (auto it = terminator.labels.rbegin();
                     it != terminator.labels.rend(); ++it) {
                    auto found = labelToBlock.find(*it);
                    if (found != labelToBlock.end() && !visited.count(found->second)) {
                        worklist.push_back(found->second);
                    }
                }
            }
        }
    }

    return result;
}

void BytecodeCompiler::compileBlock(const il::core::BasicBlock& block) {
    // Record block offset
    blockOffsets_[block.label] = static_cast<uint32_t>(currentFunc_->code.size());

    // Handle block parameters - they receive values from branch arguments
    // The calling block will have already pushed the arguments

    // Compile each instruction
    for (const auto& instr : block.instructions) {
        compileInstr(instr);
    }
}

void BytecodeCompiler::compileInstr(const il::core::Instr& instr) {
    using Opcode = il::core::Opcode;

    switch (instr.op) {
        // Arithmetic
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::UDiv:
        case Opcode::SRem:
        case Opcode::URem:
        case Opcode::IAddOvf:
        case Opcode::ISubOvf:
        case Opcode::IMulOvf:
        case Opcode::SDivChk0:
        case Opcode::UDivChk0:
        case Opcode::SRemChk0:
        case Opcode::URemChk0:
            compileArithmetic(instr);
            break;

        // Float arithmetic
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
            compileArithmetic(instr);
            break;

        // Comparisons
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
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            compileComparison(instr);
            break;

        // Conversions
        case Opcode::Sitofp:
        case Opcode::Fptosi:
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk:
        case Opcode::CastSiToFp:
        case Opcode::CastUiToFp:
        case Opcode::Zext1:
        case Opcode::Trunc1:
            compileConversion(instr);
            break;

        // Bitwise
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
            compileBitwise(instr);
            break;

        // Memory
        case Opcode::Alloca:
        case Opcode::GEP:
        case Opcode::Load:
        case Opcode::Store:
        case Opcode::AddrOf:
        case Opcode::GAddr:
        case Opcode::ConstStr:
        case Opcode::ConstNull:
            compileMemory(instr);
            break;

        // Control flow
        case Opcode::Call:
        case Opcode::CallIndirect:
            compileCall(instr);
            break;

        case Opcode::Br:
        case Opcode::CBr:
        case Opcode::SwitchI32:
            compileBranch(instr);
            break;

        case Opcode::Ret:
            compileReturn(instr);
            break;

        // Bounds check
        case Opcode::IdxChk:
            // Push operands: index, lo, hi
            pushValue(instr.operands[0]);
            pushValue(instr.operands[1]);
            pushValue(instr.operands[2]);
            emit(BCOpcode::IDX_CHK);
            popStack(2);  // Consumes 3, produces 1
            storeResult(instr);
            break;

        // Exception handling (Phase 3)
        case Opcode::TrapKind:
        case Opcode::TrapFromErr:
        case Opcode::TrapErr:
        case Opcode::ErrGetKind:
        case Opcode::ErrGetCode:
        case Opcode::ErrGetIp:
        case Opcode::ErrGetLine:
        case Opcode::EhPush:
        case Opcode::EhPop:
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
        case Opcode::EhEntry:
        case Opcode::Trap:
            // TODO: Phase 3 - Exception handling
            break;

        default:
            // Unknown opcode
            break;
    }
}

void BytecodeCompiler::pushValue(const il::core::Value& val) {
    switch (val.kind) {
        case il::core::Value::Kind::Temp: {
            uint32_t local = getLocal(val.id);
            if (local < 256) {
                emit8(BCOpcode::LOAD_LOCAL, static_cast<uint8_t>(local));
            } else {
                emit16(BCOpcode::LOAD_LOCAL_W, static_cast<uint16_t>(local));
            }
            pushStack();
            break;
        }

        case il::core::Value::Kind::ConstInt: {
            int64_t v = val.i64;
            if (v == 0) {
                emit(BCOpcode::LOAD_ZERO);
            } else if (v == 1) {
                emit(BCOpcode::LOAD_ONE);
            } else if (v >= -128 && v <= 127) {
                emitI8(BCOpcode::LOAD_I8, static_cast<int8_t>(v));
            } else if (v >= -32768 && v <= 32767) {
                emitI16(BCOpcode::LOAD_I16, static_cast<int16_t>(v));
            } else {
                // Add to constant pool
                uint32_t idx = module_.addI64(v);
                emit16(BCOpcode::LOAD_I64, static_cast<uint16_t>(idx));
            }
            pushStack();
            break;
        }

        case il::core::Value::Kind::ConstFloat: {
            uint32_t idx = module_.addF64(val.f64);
            emit16(BCOpcode::LOAD_F64, static_cast<uint16_t>(idx));
            pushStack();
            break;
        }

        case il::core::Value::Kind::ConstStr: {
            uint32_t idx = module_.addString(val.str);
            emit16(BCOpcode::LOAD_STR, static_cast<uint16_t>(idx));
            pushStack();
            break;
        }

        case il::core::Value::Kind::GlobalAddr:
            // TODO: Handle global addresses
            emit(BCOpcode::LOAD_NULL);
            pushStack();
            break;

        case il::core::Value::Kind::NullPtr:
            emit(BCOpcode::LOAD_NULL);
            pushStack();
            break;
    }
}

void BytecodeCompiler::storeResult(const il::core::Instr& instr) {
    if (instr.result) {
        uint32_t local = getLocal(*instr.result);
        if (local < 256) {
            emit8(BCOpcode::STORE_LOCAL, static_cast<uint8_t>(local));
        } else {
            emit16(BCOpcode::STORE_LOCAL_W, static_cast<uint16_t>(local));
        }
        popStack();
    } else {
        // No result - pop value if stack isn't empty
        // (some operations like stores don't produce values)
    }
}

void BytecodeCompiler::emit(uint32_t instr) {
    currentFunc_->code.push_back(instr);
}

void BytecodeCompiler::emit(BCOpcode op) {
    emit(encodeOp(op));
}

void BytecodeCompiler::emit8(BCOpcode op, uint8_t arg) {
    emit(encodeOp8(op, arg));
}

void BytecodeCompiler::emitI8(BCOpcode op, int8_t arg) {
    emit(encodeOpI8(op, arg));
}

void BytecodeCompiler::emit16(BCOpcode op, uint16_t arg) {
    emit(encodeOp16(op, arg));
}

void BytecodeCompiler::emitI16(BCOpcode op, int16_t arg) {
    emit(encodeOpI16(op, arg));
}

void BytecodeCompiler::emit88(BCOpcode op, uint8_t arg0, uint8_t arg1) {
    emit(encodeOp88(op, arg0, arg1));
}

void BytecodeCompiler::emitBranch(BCOpcode op, const std::string& label) {
    pendingBranches_.push_back({
        static_cast<uint32_t>(currentFunc_->code.size()),
        label,
        false
    });
    emit(encodeOp16(op, 0));  // Placeholder offset
}

void BytecodeCompiler::emitBranchLong(BCOpcode op, const std::string& label) {
    pendingBranches_.push_back({
        static_cast<uint32_t>(currentFunc_->code.size()),
        label,
        true
    });
    emit(encodeOp24(op, 0));  // Placeholder offset
}

void BytecodeCompiler::resolveBranches() {
    for (const auto& fixup : pendingBranches_) {
        auto it = blockOffsets_.find(fixup.targetLabel);
        if (it == blockOffsets_.end()) {
            // Unknown target - should not happen
            continue;
        }

        int32_t offset = static_cast<int32_t>(it->second) -
                         static_cast<int32_t>(fixup.codeOffset) - 1;

        uint32_t instr = currentFunc_->code[fixup.codeOffset];
        BCOpcode op = decodeOpcode(instr);

        if (fixup.isLong) {
            currentFunc_->code[fixup.codeOffset] = encodeOpI24(op, offset);
        } else {
            currentFunc_->code[fixup.codeOffset] = encodeOpI16(op, static_cast<int16_t>(offset));
        }
    }
}

void BytecodeCompiler::pushStack(int32_t count) {
    currentStackDepth_ += count;
    if (currentStackDepth_ > maxStackDepth_) {
        maxStackDepth_ = currentStackDepth_;
    }
}

void BytecodeCompiler::popStack(int32_t count) {
    currentStackDepth_ -= count;
    if (currentStackDepth_ < 0) {
        currentStackDepth_ = 0;  // Shouldn't happen, but be safe
    }
}

uint32_t BytecodeCompiler::getLocal(uint32_t ssaId) {
    auto it = ssaToLocal_.find(ssaId);
    if (it != ssaToLocal_.end()) {
        return it->second;
    }
    // Allocate new local if not found
    uint32_t local = nextLocal_++;
    ssaToLocal_[ssaId] = local;
    return local;
}

void BytecodeCompiler::compileArithmetic(const il::core::Instr& instr) {
    using Opcode = il::core::Opcode;

    // Push operands
    assert(instr.operands.size() >= 2);
    pushValue(instr.operands[0]);
    pushValue(instr.operands[1]);

    // Emit operation
    BCOpcode bcOp;
    switch (instr.op) {
        case Opcode::Add:       bcOp = BCOpcode::ADD_I64; break;
        case Opcode::Sub:       bcOp = BCOpcode::SUB_I64; break;
        case Opcode::Mul:       bcOp = BCOpcode::MUL_I64; break;
        case Opcode::SDiv:      bcOp = BCOpcode::SDIV_I64; break;
        case Opcode::UDiv:      bcOp = BCOpcode::UDIV_I64; break;
        case Opcode::SRem:      bcOp = BCOpcode::SREM_I64; break;
        case Opcode::URem:      bcOp = BCOpcode::UREM_I64; break;
        case Opcode::IAddOvf:   bcOp = BCOpcode::ADD_I64_OVF; break;
        case Opcode::ISubOvf:   bcOp = BCOpcode::SUB_I64_OVF; break;
        case Opcode::IMulOvf:   bcOp = BCOpcode::MUL_I64_OVF; break;
        case Opcode::SDivChk0:  bcOp = BCOpcode::SDIV_I64_CHK; break;
        case Opcode::UDivChk0:  bcOp = BCOpcode::UDIV_I64_CHK; break;
        case Opcode::SRemChk0:  bcOp = BCOpcode::SREM_I64_CHK; break;
        case Opcode::URemChk0:  bcOp = BCOpcode::UREM_I64_CHK; break;
        case Opcode::FAdd:      bcOp = BCOpcode::ADD_F64; break;
        case Opcode::FSub:      bcOp = BCOpcode::SUB_F64; break;
        case Opcode::FMul:      bcOp = BCOpcode::MUL_F64; break;
        case Opcode::FDiv:      bcOp = BCOpcode::DIV_F64; break;
        default:
            bcOp = BCOpcode::NOP;
    }

    emit(bcOp);
    popStack();  // Binary ops: consume 2, produce 1
    storeResult(instr);
}

void BytecodeCompiler::compileComparison(const il::core::Instr& instr) {
    using Opcode = il::core::Opcode;

    // Push operands
    assert(instr.operands.size() >= 2);
    pushValue(instr.operands[0]);
    pushValue(instr.operands[1]);

    // Emit comparison
    BCOpcode bcOp;
    switch (instr.op) {
        case Opcode::ICmpEq:  bcOp = BCOpcode::CMP_EQ_I64; break;
        case Opcode::ICmpNe:  bcOp = BCOpcode::CMP_NE_I64; break;
        case Opcode::SCmpLT:  bcOp = BCOpcode::CMP_SLT_I64; break;
        case Opcode::SCmpLE:  bcOp = BCOpcode::CMP_SLE_I64; break;
        case Opcode::SCmpGT:  bcOp = BCOpcode::CMP_SGT_I64; break;
        case Opcode::SCmpGE:  bcOp = BCOpcode::CMP_SGE_I64; break;
        case Opcode::UCmpLT:  bcOp = BCOpcode::CMP_ULT_I64; break;
        case Opcode::UCmpLE:  bcOp = BCOpcode::CMP_ULE_I64; break;
        case Opcode::UCmpGT:  bcOp = BCOpcode::CMP_UGT_I64; break;
        case Opcode::UCmpGE:  bcOp = BCOpcode::CMP_UGE_I64; break;
        case Opcode::FCmpEQ:  bcOp = BCOpcode::CMP_EQ_F64; break;
        case Opcode::FCmpNE:  bcOp = BCOpcode::CMP_NE_F64; break;
        case Opcode::FCmpLT:  bcOp = BCOpcode::CMP_LT_F64; break;
        case Opcode::FCmpLE:  bcOp = BCOpcode::CMP_LE_F64; break;
        case Opcode::FCmpGT:  bcOp = BCOpcode::CMP_GT_F64; break;
        case Opcode::FCmpGE:  bcOp = BCOpcode::CMP_GE_F64; break;
        default:
            bcOp = BCOpcode::NOP;
    }

    emit(bcOp);
    popStack();  // Binary ops: consume 2, produce 1
    storeResult(instr);
}

void BytecodeCompiler::compileConversion(const il::core::Instr& instr) {
    using Opcode = il::core::Opcode;

    // Push operand
    assert(!instr.operands.empty());
    pushValue(instr.operands[0]);

    // Emit conversion
    BCOpcode bcOp;
    switch (instr.op) {
        case Opcode::Sitofp:
        case Opcode::CastSiToFp:
            bcOp = BCOpcode::I64_TO_F64; break;
        case Opcode::CastUiToFp:
            bcOp = BCOpcode::U64_TO_F64; break;
        case Opcode::Fptosi:
            bcOp = BCOpcode::F64_TO_I64; break;
        case Opcode::CastFpToSiRteChk:
            bcOp = BCOpcode::F64_TO_I64_CHK; break;
        case Opcode::CastFpToUiRteChk:
            bcOp = BCOpcode::F64_TO_U64_CHK; break;
        case Opcode::CastSiNarrowChk:
            bcOp = BCOpcode::I64_NARROW_CHK; break;
        case Opcode::CastUiNarrowChk:
            bcOp = BCOpcode::U64_NARROW_CHK; break;
        case Opcode::Zext1:
            bcOp = BCOpcode::BOOL_TO_I64; break;
        case Opcode::Trunc1:
            bcOp = BCOpcode::I64_TO_BOOL; break;
        default:
            bcOp = BCOpcode::NOP;
    }

    emit(bcOp);
    // Unary ops: consume 1, produce 1 - no stack change
    storeResult(instr);
}

void BytecodeCompiler::compileBitwise(const il::core::Instr& instr) {
    using Opcode = il::core::Opcode;

    // Push operands
    assert(instr.operands.size() >= 2);
    pushValue(instr.operands[0]);
    pushValue(instr.operands[1]);

    // Emit operation
    BCOpcode bcOp;
    switch (instr.op) {
        case Opcode::And:   bcOp = BCOpcode::AND_I64; break;
        case Opcode::Or:    bcOp = BCOpcode::OR_I64; break;
        case Opcode::Xor:   bcOp = BCOpcode::XOR_I64; break;
        case Opcode::Shl:   bcOp = BCOpcode::SHL_I64; break;
        case Opcode::LShr:  bcOp = BCOpcode::LSHR_I64; break;
        case Opcode::AShr:  bcOp = BCOpcode::ASHR_I64; break;
        default:
            bcOp = BCOpcode::NOP;
    }

    emit(bcOp);
    popStack();  // Binary ops: consume 2, produce 1
    storeResult(instr);
}

void BytecodeCompiler::compileMemory(const il::core::Instr& instr) {
    using Opcode = il::core::Opcode;

    switch (instr.op) {
        case Opcode::ConstNull:
            emit(BCOpcode::LOAD_NULL);
            pushStack();
            storeResult(instr);
            break;

        case Opcode::ConstStr:
            // String literal - operand is a const string value
            if (!instr.operands.empty() &&
                instr.operands[0].kind == il::core::Value::Kind::ConstStr) {
                uint32_t idx = module_.addString(instr.operands[0].str);
                emit16(BCOpcode::LOAD_STR, static_cast<uint16_t>(idx));
            } else {
                emit(BCOpcode::LOAD_NULL);
            }
            pushStack();
            storeResult(instr);
            break;

        case Opcode::Alloca:
            pushValue(instr.operands[0]);  // Size
            emit(BCOpcode::ALLOCA);
            // Alloca consumes 1, produces 1 - no stack change
            storeResult(instr);
            break;

        case Opcode::GEP:
            pushValue(instr.operands[0]);  // Base pointer
            pushValue(instr.operands[1]);  // Offset
            emit(BCOpcode::GEP);
            popStack();  // Consume 2, produce 1
            storeResult(instr);
            break;

        case Opcode::Load:
            pushValue(instr.operands[1]);  // Pointer (operand 0 is type)
            // For now, assume i64 load
            emit(BCOpcode::LOAD_I64_MEM);
            // Load consumes 1, produces 1 - no stack change
            storeResult(instr);
            break;

        case Opcode::Store:
            pushValue(instr.operands[1]);  // Pointer (operand 0 is type)
            pushValue(instr.operands[2]);  // Value
            emit(BCOpcode::STORE_I64_MEM);
            popStack(2);  // Consume 2, produce 0
            break;

        case Opcode::AddrOf:
            pushValue(instr.operands[0]);
            // AddrOf is identity for pointers in bytecode
            storeResult(instr);
            break;

        case Opcode::GAddr:
            // Global address - for now emit null
            emit(BCOpcode::LOAD_NULL);
            pushStack();
            storeResult(instr);
            break;

        default:
            break;
    }
}

void BytecodeCompiler::compileCall(const il::core::Instr& instr) {
    // Push arguments
    for (const auto& arg : instr.operands) {
        pushValue(arg);
    }

    // For now, assume internal function call
    // Look up function index
    auto it = module_.functionIndex.find(instr.callee);
    if (it != module_.functionIndex.end()) {
        emit16(BCOpcode::CALL, static_cast<uint16_t>(it->second));
    } else {
        // External/native call
        uint32_t nativeIdx = module_.addNativeFunc(
            instr.callee,
            static_cast<uint32_t>(instr.operands.size()),
            instr.result.has_value());
        emit88(BCOpcode::CALL_NATIVE,
               static_cast<uint8_t>(nativeIdx),
               static_cast<uint8_t>(instr.operands.size()));
    }

    // Pop arguments, push result if any
    popStack(static_cast<int32_t>(instr.operands.size()));
    if (instr.result) {
        pushStack();
        storeResult(instr);
    }
}

void BytecodeCompiler::compileBranch(const il::core::Instr& instr) {
    using Opcode = il::core::Opcode;

    switch (instr.op) {
        case Opcode::Br: {
            // Unconditional branch
            // First, push block arguments
            if (!instr.labels.empty() && !instr.brArgs.empty() && !instr.brArgs[0].empty()) {
                for (const auto& arg : instr.brArgs[0]) {
                    pushValue(arg);
                }
                // Store to block parameter locals
                // TODO: Need to track block parameter mapping
            }
            emitBranch(BCOpcode::JUMP, instr.labels[0]);
            break;
        }

        case Opcode::CBr: {
            // Conditional branch
            pushValue(instr.operands[0]);  // Condition

            // cbr %cond, thenLabel, elseLabel
            // Emit: JUMP_IF_TRUE thenLabel; JUMP elseLabel
            // Or: JUMP_IF_FALSE elseLabel; (fall through to then)

            // TODO: Handle block arguments properly
            emitBranch(BCOpcode::JUMP_IF_FALSE, instr.labels[1]);  // Jump to else if false
            popStack();
            emitBranch(BCOpcode::JUMP, instr.labels[0]);  // Jump to then
            break;
        }

        case Opcode::SwitchI32: {
            // Switch statement
            pushValue(il::core::switchScrutinee(instr));
            // TODO: Build switch table and emit SWITCH opcode
            emit(BCOpcode::POP);
            popStack();
            break;
        }

        default:
            break;
    }
}

void BytecodeCompiler::compileReturn(const il::core::Instr& instr) {
    if (!instr.operands.empty()) {
        pushValue(instr.operands[0]);
        emit(BCOpcode::RETURN);
        popStack();
    } else {
        emit(BCOpcode::RETURN_VOID);
    }
}

} // namespace bytecode
} // namespace viper
