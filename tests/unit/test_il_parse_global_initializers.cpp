// File: tests/unit/test_il_parse_global_initializers.cpp
// Purpose: Validate parsing of typed global initializers.
// Key invariants: Parsed globals capture type and literal/value kind accurately.
// Ownership/Lifetime: Test owns module and parsing stream.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>

int main()
{
    const char *source = R"(il 0.1.2
global i64 @counter = 42
global const f64 @ratio = 3.5
global const str @message = "ok"
global ptr @message_ptr = @message
global ptr @nil = null
func @main() -> void {
entry:
  ret
}
)";

    std::istringstream input(source);
    il::core::Module module;
    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(parsed);

    assert(module.globals.size() == 5);

    auto findGlobal = [&](const char *name) -> const il::core::Global & {
        auto it = std::find_if(module.globals.begin(), module.globals.end(),
                               [&](const il::core::Global &g) { return g.name == name; });
        assert(it != module.globals.end());
        return *it;
    };

    const auto &counter = findGlobal("counter");
    assert(counter.type.kind == il::core::Type::Kind::I64);
    assert(counter.init.kind == il::core::Value::Kind::ConstInt);
    assert(counter.init.i64 == 42);
    assert(!counter.isConst);

    const auto &ratio = findGlobal("ratio");
    assert(ratio.type.kind == il::core::Type::Kind::F64);
    assert(ratio.init.kind == il::core::Value::Kind::ConstFloat);
    assert(ratio.isConst);
    assert(ratio.init.f64 == 3.5);

    const auto &message = findGlobal("message");
    assert(message.type.kind == il::core::Type::Kind::Str);
    assert(message.init.kind == il::core::Value::Kind::ConstStr);
    assert(message.init.str == std::string("ok"));
    assert(message.isConst);

    const auto &ptr = findGlobal("message_ptr");
    assert(ptr.type.kind == il::core::Type::Kind::Ptr);
    assert(ptr.init.kind == il::core::Value::Kind::GlobalAddr);
    assert(ptr.init.str == std::string("message"));
    assert(!ptr.isConst);

    const auto &nil = findGlobal("nil");
    assert(nil.type.kind == il::core::Type::Kind::Ptr);
    assert(nil.init.kind == il::core::Value::Kind::NullPtr);

    return 0;
}
