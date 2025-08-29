#include "il/io/Serializer.h"
#include "il/core/Opcode.h"
#include "il/core/Value.h"
#include <sstream>

namespace il::io {

using namespace il::core;

void Serializer::write(const Module &m, std::ostream &os) {
  os << "il 0.1\n";
  for (const auto &e : m.externs) {
    os << "extern @" << e.name << "(";
    for (size_t i = 0; i < e.params.size(); ++i) {
      if (i)
        os << ", ";
      os << e.params[i].toString();
    }
    os << ") -> " << e.retType.toString() << "\n";
  }
  for (const auto &g : m.globals) {
    os << "global const " << g.type.toString() << " @" << g.name << " = \"" << g.init << "\"\n";
  }
  for (const auto &f : m.functions) {
    os << "func @" << f.name << "(";
    for (size_t i = 0; i < f.params.size(); ++i) {
      if (i)
        os << ", ";
      os << f.params[i].type.toString() << " %" << f.params[i].name;
    }
    os << ") -> " << f.retType.toString() << " {\n";
    for (const auto &bb : f.blocks) {
      os << bb.label << ":\n";
      for (const auto &in : bb.instructions) {
        os << "  ";
        if (in.result)
          os << "%" << *in.result << " = ";
        os << il::core::toString(in.op);
        if (in.op == Opcode::Call) {
          os << " @" << in.callee << "(";
          for (size_t i = 0; i < in.operands.size(); ++i) {
            if (i)
              os << ", ";
            os << il::core::toString(in.operands[i]);
          }
          os << ")";
        } else if (in.op == Opcode::Ret) {
          if (!in.operands.empty())
            os << " " << il::core::toString(in.operands[0]);
        } else if (in.op == Opcode::Br) {
          if (!in.labels.empty())
            os << " label " << in.labels[0];
        } else if (in.op == Opcode::CBr) {
          os << " " << il::core::toString(in.operands[0]) << ", label " << in.labels[0]
             << ", label " << in.labels[1];
        } else if (in.op == Opcode::Load) {
          os << " " << in.type.toString() << ", " << il::core::toString(in.operands[0]);
        } else if (in.op == Opcode::Store) {
          os << " " << in.type.toString() << ", " << il::core::toString(in.operands[0]) << ", "
             << il::core::toString(in.operands[1]);
        } else {
          if (!in.operands.empty())
            os << " ";
          for (size_t i = 0; i < in.operands.size(); ++i) {
            if (i)
              os << ", ";
            os << il::core::toString(in.operands[i]);
          }
        }
        os << "\n";
      }
    }
    os << "}\n";
  }
}

std::string Serializer::toString(const Module &m) {
  std::ostringstream oss;
  write(m, oss);
  return oss.str();
}

} // namespace il::io
