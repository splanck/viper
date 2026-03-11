//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia-server/test_transport.cpp
// Purpose: Unit tests for MCP and LSP transport framing and JSON-RPC helpers.
// Key invariants:
//   - MCP: newline-delimited messages round-trip correctly
//   - LSP: Content-Length framed messages round-trip correctly
//   - JSON-RPC: request parsing and response building conform to spec
// Ownership/Lifetime:
//   - Test-only file
// Links: tools/zia-server/Transport.hpp, tools/zia-server/JsonRpc.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include "tools/zia-server/Json.hpp"
#include "tools/zia-server/JsonRpc.hpp"
#include "tools/zia-server/Transport.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace viper::server;

// --- Helper: create a FILE* from a string for reading ---

static FILE *memRead(const std::string &data)
{
    FILE *f = std::tmpfile();
    if (!f)
        return nullptr;
    std::fwrite(data.data(), 1, data.size(), f);
    std::rewind(f);
    return f;
}

// --- Helper: read all content from a FILE* ---

static std::string memReadAll(FILE *f)
{
    std::rewind(f);
    std::string result;
    char buf[256];
    while (true)
    {
        size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n == 0)
            break;
        result.append(buf, n);
    }
    return result;
}

// ===== MCP Transport =====

TEST(McpTransport, ReadSingleMessage)
{
    FILE *in = memRead("{\"jsonrpc\":\"2.0\"}\n");
    ASSERT_TRUE(in != nullptr);
    FILE *out = std::tmpfile();

    McpTransport transport(in, out);
    RawMessage msg;
    EXPECT_TRUE(transport.readMessage(msg));
    EXPECT_EQ(msg.content, "{\"jsonrpc\":\"2.0\"}");

    std::fclose(in);
    std::fclose(out);
}

TEST(McpTransport, ReadMultipleMessages)
{
    FILE *in = memRead("{\"a\":1}\n{\"b\":2}\n");
    ASSERT_TRUE(in != nullptr);
    FILE *out = std::tmpfile();

    McpTransport transport(in, out);
    RawMessage msg;

    EXPECT_TRUE(transport.readMessage(msg));
    EXPECT_EQ(msg.content, "{\"a\":1}");

    EXPECT_TRUE(transport.readMessage(msg));
    EXPECT_EQ(msg.content, "{\"b\":2}");

    EXPECT_FALSE(transport.readMessage(msg)); // EOF

    std::fclose(in);
    std::fclose(out);
}

TEST(McpTransport, ReadCRLF)
{
    FILE *in = memRead("{\"x\":1}\r\n");
    ASSERT_TRUE(in != nullptr);
    FILE *out = std::tmpfile();

    McpTransport transport(in, out);
    RawMessage msg;
    EXPECT_TRUE(transport.readMessage(msg));
    EXPECT_EQ(msg.content, "{\"x\":1}");

    std::fclose(in);
    std::fclose(out);
}

TEST(McpTransport, SkipsEmptyLines)
{
    FILE *in = memRead("\n\n{\"x\":1}\n\n");
    ASSERT_TRUE(in != nullptr);
    FILE *out = std::tmpfile();

    McpTransport transport(in, out);
    RawMessage msg;
    EXPECT_TRUE(transport.readMessage(msg));
    EXPECT_EQ(msg.content, "{\"x\":1}");

    std::fclose(in);
    std::fclose(out);
}

TEST(McpTransport, WriteMessage)
{
    FILE *in = memRead("");
    FILE *out = std::tmpfile();
    ASSERT_TRUE(out != nullptr);

    McpTransport transport(in, out);
    transport.writeMessage("{\"result\":true}");

    std::string written = memReadAll(out);
    EXPECT_EQ(written, "{\"result\":true}\n");

    std::fclose(in);
    std::fclose(out);
}

TEST(McpTransport, EndOfFile)
{
    FILE *in = memRead("");
    FILE *out = std::tmpfile();

    McpTransport transport(in, out);
    RawMessage msg;
    EXPECT_FALSE(transport.readMessage(msg));

    std::fclose(in);
    std::fclose(out);
}

// ===== LSP Transport =====

TEST(LspTransport, ReadSingleMessage)
{
    std::string data = "Content-Length: 18\r\n\r\n{\"jsonrpc\":\"2.0\"}";
    // Note: the JSON is 17 chars, but let's use exact length
    std::string json = "{\"jsonrpc\":\"2.0\"}";
    std::string frame = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;

    FILE *in = memRead(frame);
    ASSERT_TRUE(in != nullptr);
    FILE *out = std::tmpfile();

    LspTransport transport(in, out);
    RawMessage msg;
    EXPECT_TRUE(transport.readMessage(msg));
    EXPECT_EQ(msg.content, json);

    std::fclose(in);
    std::fclose(out);
}

TEST(LspTransport, ReadMultipleMessages)
{
    std::string json1 = "{\"a\":1}";
    std::string json2 = "{\"b\":2}";
    std::string frame = "Content-Length: " + std::to_string(json1.size()) + "\r\n\r\n" + json1 +
                        "Content-Length: " + std::to_string(json2.size()) + "\r\n\r\n" + json2;

    FILE *in = memRead(frame);
    ASSERT_TRUE(in != nullptr);
    FILE *out = std::tmpfile();

    LspTransport transport(in, out);
    RawMessage msg;

    EXPECT_TRUE(transport.readMessage(msg));
    EXPECT_EQ(msg.content, json1);

    EXPECT_TRUE(transport.readMessage(msg));
    EXPECT_EQ(msg.content, json2);

    std::fclose(in);
    std::fclose(out);
}

TEST(LspTransport, WriteMessage)
{
    FILE *in = memRead("");
    FILE *out = std::tmpfile();
    ASSERT_TRUE(out != nullptr);

    LspTransport transport(in, out);
    transport.writeMessage("{\"result\":42}");

    std::string written = memReadAll(out);
    EXPECT_EQ(written, "Content-Length: 13\r\n\r\n{\"result\":42}");

    std::fclose(in);
    std::fclose(out);
}

TEST(LspTransport, IgnoresOtherHeaders)
{
    std::string json = "{\"x\":1}";
    std::string frame =
        "Content-Type: application/json\r\nContent-Length: " + std::to_string(json.size()) +
        "\r\n\r\n" + json;

    FILE *in = memRead(frame);
    ASSERT_TRUE(in != nullptr);
    FILE *out = std::tmpfile();

    LspTransport transport(in, out);
    RawMessage msg;
    EXPECT_TRUE(transport.readMessage(msg));
    EXPECT_EQ(msg.content, json);

    std::fclose(in);
    std::fclose(out);
}

// ===== JSON-RPC =====

TEST(JsonRpc, ParseRequest)
{
    auto msg = JsonValue::parse(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"x\":1}}");
    JsonRpcRequest req;
    EXPECT_TRUE(parseRequest(msg, req));
    EXPECT_EQ(req.method, "initialize");
    EXPECT_EQ(req.id.asInt(), 1);
    EXPECT_EQ(req.params["x"].asInt(), 1);
    EXPECT_FALSE(req.isNotification());
}

TEST(JsonRpc, ParseNotification)
{
    auto msg = JsonValue::parse("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\"}");
    JsonRpcRequest req;
    EXPECT_TRUE(parseRequest(msg, req));
    EXPECT_EQ(req.method, "initialized");
    EXPECT_TRUE(req.isNotification());
}

TEST(JsonRpc, ParseRequestStringId)
{
    auto msg = JsonValue::parse("{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"test\"}");
    JsonRpcRequest req;
    EXPECT_TRUE(parseRequest(msg, req));
    EXPECT_EQ(req.id.asString(), "abc");
}

TEST(JsonRpc, ParseInvalidNoMethod)
{
    auto msg = JsonValue::parse("{\"jsonrpc\":\"2.0\",\"id\":1}");
    JsonRpcRequest req;
    EXPECT_FALSE(parseRequest(msg, req));
}

TEST(JsonRpc, BuildResponse)
{
    auto json = buildResponse(JsonValue(1), JsonValue("ok"));
    auto parsed = JsonValue::parse(json);
    EXPECT_EQ(parsed["jsonrpc"].asString(), "2.0");
    EXPECT_EQ(parsed["id"].asInt(), 1);
    EXPECT_EQ(parsed["result"].asString(), "ok");
}

TEST(JsonRpc, BuildError)
{
    auto json = buildError(JsonValue(1), kMethodNotFound, "no such method");
    auto parsed = JsonValue::parse(json);
    EXPECT_EQ(parsed["jsonrpc"].asString(), "2.0");
    EXPECT_EQ(parsed["id"].asInt(), 1);
    EXPECT_EQ(parsed["error"]["code"].asInt(), -32601);
    EXPECT_EQ(parsed["error"]["message"].asString(), "no such method");
}

TEST(JsonRpc, BuildErrorWithData)
{
    auto json = buildError(JsonValue(1), kInternalError, "oops", JsonValue("details"));
    auto parsed = JsonValue::parse(json);
    EXPECT_EQ(parsed["error"]["data"].asString(), "details");
}

TEST(JsonRpc, BuildNotification)
{
    auto json = buildNotification("textDocument/publishDiagnostics",
                                  JsonValue::object({{"uri", JsonValue("file:///test.zia")}}));
    auto parsed = JsonValue::parse(json);
    EXPECT_EQ(parsed["jsonrpc"].asString(), "2.0");
    EXPECT_EQ(parsed["method"].asString(), "textDocument/publishDiagnostics");
    EXPECT_EQ(parsed["params"]["uri"].asString(), "file:///test.zia");
    EXPECT_TRUE(parsed.get("id") == nullptr);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
