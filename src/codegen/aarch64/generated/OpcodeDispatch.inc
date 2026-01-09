//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// IMPORTANT: This file is auto-generated. Do not edit manually.
//
// File: codegen/aarch64/generated/OpcodeDispatch.inc
// Purpose: Opcode dispatch switch logic for AArch64 instruction emission.
//
// Generator: cmake/GenAArch64Dispatch.cmake
// Source: tools/spec/aarch64_encodings.json
// Regenerate: cmake --build build (automatic on spec change)
//
// Description: Contains the switch statement body that maps MOpcode enum
//              values to their corresponding emit* function calls.
//
// Links: docs/il-guide.md#reference, codegen/aarch64/AsmEmitter.cpp
//
//===----------------------------------------------------------------------===//

using K = MOpcode;
auto reg = [](const MOperand &op)
{
    assert(op.kind == MOperand::Kind::Reg && "expected reg operand");
    assert(op.reg.isPhys && "unallocated vreg reached emitter");
    return static_cast<PhysReg>(op.reg.idOrPhys);
};
auto imm = [](const MOperand &op) { return op.imm; };

switch (mi.opc)
{
    case K::MovRR:
        emitMovRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
        break;
    case K::MovRI:
    {
        const long long v = imm(mi.ops[1]);
        if (v >= 0 && v <= 65535)
            emitMovRI(os, reg(mi.ops[0]), v);
        else
            emitMovImm64(os, reg(mi.ops[0]), static_cast<unsigned long long>(v));
        break;
    }
    case K::FMovRR:
        emitFMovRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
        break;
    case K::FMovRI:
    {
        const long long bits = imm(mi.ops[1]);
        double dv;
        static_assert(sizeof(long long) == sizeof(double), "size");
        std::memcpy(&dv, &bits, sizeof(double));
        emitFMovRI(os, reg(mi.ops[0]), dv);
        break;
    }
    case K::FAddRRR:
        emitFAddRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::FSubRRR:
        emitFSubRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::FMulRRR:
        emitFMulRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::FDivRRR:
        emitFDivRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::FCmpRR:
        emitFCmpRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
        break;
    case K::SCvtF:
        emitSCvtF(os, reg(mi.ops[0]), reg(mi.ops[1]));
        break;
    case K::FCvtZS:
        emitFCvtZS(os, reg(mi.ops[0]), reg(mi.ops[1]));
        break;
    case K::UCvtF:
        emitUCvtF(os, reg(mi.ops[0]), reg(mi.ops[1]));
        break;
    case K::FCvtZU:
        emitFCvtZU(os, reg(mi.ops[0]), reg(mi.ops[1]));
        break;
    case K::AddRRR:
        emitAddRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::SubRRR:
        emitSubRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::MulRRR:
        emitMulRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::AndRRR:
        emitAndRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::OrrRRR:
        emitOrrRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::EorRRR:
        emitEorRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
        break;
    case K::AddRI:
        emitAddRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
        break;
    case K::SubRI:
        emitSubRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
        break;
    case K::LslRI:
        emitLslRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
        break;
    case K::LsrRI:
        emitLsrRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
        break;
    case K::AsrRI:
        emitAsrRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
        break;
    case K::CmpRR:
        emitCmpRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
        break;
    case K::CmpRI:
        emitCmpRI(os, reg(mi.ops[0]), imm(mi.ops[1]));
        break;
    case K::TstRR:
        emitTstRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
        break;
    case K::Cset:
        emitCset(os, reg(mi.ops[0]), mi.ops[1].cond);
        break;
    case K::SubSpImm:
        emitSubSp(os, imm(mi.ops[0]));
        break;
    case K::AddSpImm:
        emitAddSp(os, imm(mi.ops[0]));
        break;
    case K::StrRegSpImm:
        emitStrToSp(os, reg(mi.ops[0]), imm(mi.ops[1]));
        break;
    case K::StrFprSpImm:
        emitStrFprToSp(os, reg(mi.ops[0]), imm(mi.ops[1]));
        break;
    case K::LdrRegFpImm:
        emitLdrFromFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
        break;
    case K::StrRegFpImm:
        emitStrToFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
        break;
    case K::LdrFprFpImm:
        emitLdrFprFromFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
        break;
    case K::StrFprFpImm:
        emitStrFprToFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
        break;
    case K::LdrRegBaseImm:
        emitLdrFromBase(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
        break;
    case K::StrRegBaseImm:
        emitStrToBase(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
        break;
    case K::Br:
        os << "  b " << mi.ops[0].label << "\n";
        break;
    case K::BCond:
        os << "  b." << mi.ops[0].cond << " " << mi.ops[1].label << "\n";
        break;
    case K::Bl:
        os << "  bl " << mangleCallTarget(mi.ops[0].label) << "\n";
        break;
    case K::AdrPage:
        os << "  adrp " << rn(reg(mi.ops[0])) << ", " << mangleSymbol(mi.ops[1].label) << "@PAGE\n";
        break;
    case K::AddPageOff:
        os << "  add " << rn(reg(mi.ops[0])) << ", " << rn(reg(mi.ops[1])) << ", "
           << mangleSymbol(mi.ops[2].label) << "@PAGEOFF\n";
        break;
    default:
        os << "  # <unknown opcode>\n";
        break;
}
