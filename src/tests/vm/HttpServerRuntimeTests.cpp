//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: src/tests/vm/HttpServerRuntimeTests.cpp
// Purpose: Validate VM-side HttpServer.BindHandler integration.
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "rt_http_server.h"
#include "rt_object.h"
#include "rt_string.h"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include <cassert>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

int main() {
    using namespace il::core;

    Module m;
    il::build::IRBuilder b(m);

    b.addExtern("Viper.Network.ServerRes.Send",
                Type(Type::Kind::Void),
                {Type(Type::Kind::Ptr), Type(Type::Kind::Str)});

    auto &handler =
        b.startFunction("handler",
                        Type(Type::Kind::Void),
                        {{"req", Type(Type::Kind::Ptr)}, {"res", Type(Type::Kind::Ptr)}});
    auto &entry = b.createBlock(handler, "entry", handler.params);
    b.setInsertPoint(entry);

    Instr makeStr;
    makeStr.result = b.reserveTempId();
    makeStr.op = Opcode::ConstStr;
    makeStr.type = Type(Type::Kind::Str);
    makeStr.operands.push_back(Value::constStr("vm-ok"));
    makeStr.loc = {1, 1, 1};
    entry.instructions.push_back(makeStr);

    b.emitCall("Viper.Network.ServerRes.Send",
               {b.blockParam(entry, 1), Value::temp(*makeStr.result)},
               std::optional<Value>{},
               {1, 1, 2});
    b.emitRet(std::optional<Value>{}, {1, 1, 3});

    il::vm::VM vm(m);

    void *server = rt_http_server_new(8080);
    rt_http_server_get(server, rt_const_cstr("/ping"), rt_const_cstr("handler"));

    il::vm::RuntimeCallContext ctx{};
    il::vm::Slot serverArg{};
    serverArg.ptr = server;
    il::vm::Slot tagArg{};
    tagArg.str = rt_const_cstr("handler");
    il::vm::Slot entryArg{};
    entryArg.ptr = const_cast<il::core::Function *>(&handler);

    {
        il::vm::ActiveVMGuard guard(&vm);
        (void)il::vm::RuntimeBridge::call(ctx,
                                          "Viper.Network.HttpServer.BindHandler",
                                          std::vector<il::vm::Slot>{serverArg, tagArg, entryArg},
                                          {},
                                          "bind_http_handler",
                                          "entry");
    }

    rt_string request = rt_const_cstr("GET /ping HTTP/1.1\r\nHost: example.test\r\n\r\n");
    rt_string response = (rt_string)rt_http_server_process_request(server, request);
    const char *responseCstr = rt_string_cstr(response);
    assert(responseCstr != nullptr);
    assert(std::strstr(responseCstr, "HTTP/1.1 200 OK\r\n") != nullptr);
    assert(std::strstr(responseCstr, "\r\n\r\nvm-ok") != nullptr);

    rt_string_unref(response);
    rt_string_unref(tagArg.str);
    if (rt_obj_release_check0(server))
        rt_obj_free(server);
    return 0;
}
