#include "il/build/IRBuilder.hpp"
#include "viper/il/IO.hpp"
#include <cassert>
#include <fstream>
#include <sstream>

int main()
{
    il::core::Module m;
    m.target = std::string("x86_64-unknown-linux-gnu");
    il::build::IRBuilder builder(m);
    builder.addExtern("rt_print_str",
                      il::core::Type(il::core::Type::Kind::Void),
                      {il::core::Type(il::core::Type::Kind::Str)});
    builder.addGlobalStr(".L0", "HELLO");
    auto &fn = builder.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);
    il::core::Value s0 = builder.emitConstStr(".L0", {});
    builder.emitCall("rt_print_str", {s0}, std::optional<il::core::Value>{}, {});
    builder.emitRet(il::core::Value::constInt(0), {});
    std::string out = il::io::Serializer::toString(m);
    std::ifstream in(std::string(TESTS_DIR) + "/golden/hello_expected.il");
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
