// test_resp.cpp — RESP2 parser/serializer unit tests
#include "gtest_compat.h"
#include "protocol/resp_parser.h"
#include <string>

using namespace minired::protocol;

// ── Serializer tests ─────────────────────────────────────────────────────────
TEST(Serializer, SimpleString) {
    EXPECT_EQ(serialize_simple("OK"), "+OK\r\n");
    EXPECT_EQ(serialize_simple("PONG"), "+PONG\r\n");
}

TEST(Serializer, ErrorString) {
    EXPECT_EQ(serialize_error("ERR bad"), "-ERR bad\r\n");
}

TEST(Serializer, Integer) {
    EXPECT_EQ(serialize_integer(42),  ":42\r\n");
    EXPECT_EQ(serialize_integer(-1),  ":-1\r\n");
    EXPECT_EQ(serialize_integer(0),   ":0\r\n");
}

TEST(Serializer, BulkString) {
    EXPECT_EQ(serialize_bulk("hello"), "$5\r\nhello\r\n");
    EXPECT_EQ(serialize_bulk(""),      "$0\r\n\r\n");
}

TEST(Serializer, NullBulk) {
    EXPECT_EQ(serialize_null_bulk(), "$-1\r\n");
}

TEST(Serializer, Array) {
    auto r = serialize_array({"foo", "bar"});
    EXPECT_EQ(r, "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
}

// ── Parser tests ─────────────────────────────────────────────────────────────
TEST(Parser, PingArray) {
    RespParser p;
    std::string msg = "*1\r\n$4\r\nPING\r\n";
    EXPECT_TRUE(p.feed(msg.data(), msg.size()));
    EXPECT_TRUE(p.has_command());
    auto cmd = p.pop_command();
    EXPECT_EQ(cmd.size(), 1u);
    EXPECT_EQ(cmd[0], "PING");
}

TEST(Parser, SetGetArray) {
    RespParser p;
    std::string msg = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    EXPECT_TRUE(p.feed(msg.data(), msg.size()));
    auto cmd = p.pop_command();
    EXPECT_EQ(cmd.size(), 3u);
    EXPECT_EQ(cmd[0], "SET");
    EXPECT_EQ(cmd[1], "foo");
    EXPECT_EQ(cmd[2], "bar");
}

TEST(Parser, InlineCommand) {
    RespParser p;
    std::string msg = "PING\r\n";
    EXPECT_TRUE(p.feed(msg.data(), msg.size()));
    auto cmd = p.pop_command();
    EXPECT_EQ(cmd.size(), 1u);
    EXPECT_EQ(cmd[0], "PING");
}

TEST(Parser, InlineMultiArg) {
    RespParser p;
    std::string msg = "SET foo bar\r\n";
    EXPECT_TRUE(p.feed(msg.data(), msg.size()));
    auto cmd = p.pop_command();
    EXPECT_EQ(cmd.size(), 3u);
    EXPECT_EQ(cmd[2], "bar");
}

TEST(Parser, PartialRead) {
    RespParser p;
    std::string full = "*1\r\n$4\r\nPING\r\n";
    // Feed byte by byte — should be safe
    bool got = false;
    for (size_t i = 0; i < full.size(); ++i) {
        bool ready = p.feed(&full[i], 1);
        if (ready) got = true;
    }
    EXPECT_TRUE(got);
    auto cmd = p.pop_command();
    EXPECT_EQ(cmd[0], "PING");
}

TEST(Parser, MultipleCommandsInOneFeed) {
    RespParser p;
    std::string msg = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
    p.feed(msg.data(), msg.size());
    EXPECT_TRUE(p.has_command()); p.pop_command();
    EXPECT_TRUE(p.has_command()); p.pop_command();
    EXPECT_FALSE(p.has_command());
}

TEST(Parser, BinaryBulkWithEmbeddedCRLF) {
    // Key containing \r\n in the value (RESP bulk allows this)
    RespParser p;
    std::string val  = "he\r\nllo";
    std::string msg  = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$7\r\n" + val + "\r\n";
    p.feed(msg.data(), msg.size());
    EXPECT_TRUE(p.has_command());
    auto cmd = p.pop_command();
    EXPECT_EQ(cmd[2], val);
}
