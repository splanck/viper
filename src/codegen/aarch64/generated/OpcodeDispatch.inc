// Generated from tools/spec/aarch64_encodings.json
// DO NOT EDIT - regenerate with tools/gen_aarch64_encodings.py

// This file contains the opcode dispatch logic for emitInstruction

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
        os << "  bl " << mi.ops[0].label << "\n";
        break;
    case K::AdrPage:
        os << "  adrp " << rn(reg(mi.ops[0])) << ", " << mi.ops[1].label << "@PAGE\n";
        break;
    case K::AddPageOff:
        os << "  add " << rn(reg(mi.ops[0])) << ", " << rn(reg(mi.ops[1])) << ", "
           << mi.ops[2].label << "@PAGEOFF\n";
        break;
    default:
        os << "  # <unknown opcode>\n";
        break;
}
