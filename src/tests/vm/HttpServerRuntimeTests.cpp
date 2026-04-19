//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: src/tests/vm/HttpServerRuntimeTests.cpp
// Purpose: Validate VM-side HttpServer.BindHandler integration.
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "rt_http_server.h"
#include "rt_https_server.h"
#include "rt_object.h"
#include "rt_string.h"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using il::runtime::signatures::make_signature;
using il::runtime::signatures::SigParam;

static void extern_http_selector(void **args, void *result) {
    (void)args;
    *static_cast<int64_t *>(result) = 1;
}

extern "C" void *rt_https_server_process_request(void *server, rt_string raw_request);

static const char *LOCALHOST_TEST_KEY_PEM = R"PEM(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg+Z1xhQRSU9+jKQhH
9R9DeB1DObDrQG6uuJYh2fGU/gOhRANCAATfYC4JF5vgz0f005FgdcIvzq+XWoK2
WkHv9ylmizkXwiiwONBMUiHLJp0aQ5prsy/qG1qvxIA+EemN8nsM73O/
-----END PRIVATE KEY-----
)PEM";

static const char *LOCALHOST_TEST_CERT_PEM = R"PEM(-----BEGIN CERTIFICATE-----
MIIBmTCCAT+gAwIBAgIUMx/aHjSr1BLKVJWLkjEW8tVBwEwwCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDQxOTAwNDY0OVoXDTM2MDQxNjAw
NDY0OVowFDESMBAGA1UEAwwJbG9jYWxob3N0MFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAE32AuCReb4M9H9NORYHXCL86vl1qCtlpB7/cpZos5F8IosDjQTFIhyyad
GkOaa7Mv6htar8SAPhHpjfJ7DO9zv6NvMG0wHQYDVR0OBBYEFH8rprP1CxiHqLBg
7tp3in6Op8rZMB8GA1UdIwQYMBaAFH8rprP1CxiHqLBg7tp3in6Op8rZMA8GA1Ud
EwEB/wQFMAMBAf8wGgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMAoGCCqGSM49
BAMCA0gAMEUCICfrWIQjaBKJOeHsEFydx3kmB3xZA27GVaokzpkBKShNAiEApv2B
ptOACq7G5MbeXCED94+Klf9Txx0gZ+qg8GckbdA=
-----END CERTIFICATE-----
)PEM";

struct TempTlsFiles {
    std::string cert_path;
    std::string key_path;

    ~TempTlsFiles() {
        std::error_code ec;
        if (!cert_path.empty())
            std::filesystem::remove(cert_path, ec);
        if (!key_path.empty())
            std::filesystem::remove(key_path, ec);
    }
};

static bool write_text_file(const std::string &path, const char *contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(contents, static_cast<std::streamsize>(std::strlen(contents)));
    return out.good();
}

static TempTlsFiles create_temp_tls_files() {
    TempTlsFiles files;
    std::error_code ec;
    const auto temp_dir = std::filesystem::temp_directory_path(ec);
    if (ec)
        return files;

    files.cert_path = (temp_dir / "viper_vm_https_cert.pem").string();
    files.key_path = (temp_dir / "viper_vm_https_key.pem").string();
    if (!write_text_file(files.cert_path, LOCALHOST_TEST_CERT_PEM) ||
        !write_text_file(files.key_path, LOCALHOST_TEST_KEY_PEM)) {
        files.cert_path.clear();
        files.key_path.clear();
    }
    return files;
}

int main() {
    using namespace il::core;

    Module m;
    il::build::IRBuilder b(m);

    b.addExtern("Viper.Network.ServerRes.Send",
                Type(Type::Kind::Void),
                {Type(Type::Kind::Ptr), Type(Type::Kind::Str)});
    b.addExtern("test.http.selector", Type(Type::Kind::I64), {});

    auto &handler =
        b.startFunction("handler",
                        Type(Type::Kind::Void),
                        {{"req", Type(Type::Kind::Ptr)}, {"res", Type(Type::Kind::Ptr)}});
    auto &entry = b.createBlock(handler, "entry", handler.params);
    b.setInsertPoint(entry);

    Instr selectorCall;
    selectorCall.result = b.reserveTempId();
    selectorCall.op = Opcode::Call;
    selectorCall.type = Type(Type::Kind::I64);
    selectorCall.callee = "test.http.selector";
    selectorCall.loc = {1, 1, 1};
    entry.instructions.push_back(selectorCall);

    Instr makeStr;
    makeStr.result = b.reserveTempId();
    makeStr.op = Opcode::ConstStr;
    makeStr.type = Type(Type::Kind::Str);
    makeStr.operands.push_back(Value::constStr("per-vm-ok"));
    makeStr.loc = {1, 1, 1};
    entry.instructions.push_back(makeStr);

    b.emitCall("Viper.Network.ServerRes.Send",
               {b.blockParam(entry, 1), Value::temp(*makeStr.result)},
               std::optional<Value>{},
               {1, 1, 2});
    b.emitRet(std::optional<Value>{}, {1, 1, 3});

    il::vm::VM vm(m);
    auto reg = il::vm::createExternRegistry();
    il::vm::ExternDesc desc;
    desc.name = "test.http.selector";
    desc.signature = make_signature("test.http.selector", {}, {SigParam::I64});
    desc.fn = reinterpret_cast<void *>(&extern_http_selector);
    il::vm::registerExternIn(*reg, desc);
    vm.setExternRegistry(reg.get());
    reg.reset();

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
    assert(std::strstr(responseCstr, "\r\n\r\nper-vm-ok") != nullptr);

    rt_string_unref(response);
    if (rt_obj_release_check0(server))
        rt_obj_free(server);

    TempTlsFiles tls_files = create_temp_tls_files();
    assert(!tls_files.cert_path.empty());
    assert(!tls_files.key_path.empty());

    void *https_server = rt_https_server_new(8443,
                                             rt_const_cstr(tls_files.cert_path.c_str()),
                                             rt_const_cstr(tls_files.key_path.c_str()));
    rt_https_server_get(https_server, rt_const_cstr("/ping"), rt_const_cstr("handler"));

    {
        il::vm::Slot httpsServerArg{};
        httpsServerArg.ptr = https_server;
        il::vm::Slot httpsTagArg{};
        httpsTagArg.str = rt_const_cstr("handler");
        il::vm::ActiveVMGuard guard(&vm);
        (void)il::vm::RuntimeBridge::call(ctx,
                                          "Viper.Network.HttpsServer.BindHandler",
                                          std::vector<il::vm::Slot>{httpsServerArg, httpsTagArg, entryArg},
                                          {},
                                          "bind_https_handler",
                                          "entry");
        rt_string_unref(httpsTagArg.str);
    }

    rt_string https_request = rt_const_cstr("GET /ping HTTP/1.1\r\nHost: example.test\r\n\r\n");
    rt_string https_response =
        (rt_string)rt_https_server_process_request(https_server, https_request);
    const char *https_response_cstr = rt_string_cstr(https_response);
    assert(https_response_cstr != nullptr);
    assert(std::strstr(https_response_cstr, "HTTP/1.1 200 OK\r\n") != nullptr);
    assert(std::strstr(https_response_cstr, "\r\n\r\nper-vm-ok") != nullptr);

    rt_string_unref(https_response);
    rt_string_unref(tagArg.str);
    if (rt_obj_release_check0(https_server))
        rt_obj_free(https_server);
    return 0;
}
