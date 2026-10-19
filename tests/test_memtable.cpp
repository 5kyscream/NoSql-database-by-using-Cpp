// test_memtable.cpp — DB integration tests covering the full LSM stack
#include "gtest_compat.h"
#include "storage/db.h"
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>

using namespace minired::storage;
namespace fs = std::filesystem;

// ── Test helpers ─────────────────────────────────────────────────────────────
static std::string tmp_dir(const std::string& name) {
    std::string dir = std::string("/tmp/minired_test_") + name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// ─────────────────────────────────────────────────────────────────────────────
TEST(DB, SetGet) {
    auto dir = tmp_dir("setget");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    db.set("foo", "bar");
    auto v = db.get("foo");
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, "bar");
    db.close();
}

TEST(DB, GetMissing) {
    auto dir = tmp_dir("missing");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    EXPECT_FALSE(db.get("nosuchkey").has_value());
    db.close();
}

TEST(DB, Delete) {
    auto dir = tmp_dir("del");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    db.set("k", "v");
    EXPECT_TRUE(db.del("k"));
    EXPECT_FALSE(db.get("k").has_value());
    EXPECT_FALSE(db.del("k"));
    db.close();
}

TEST(DB, Expiry) {
    auto dir = tmp_dir("expiry");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    using namespace std::chrono;
    int64_t exp = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count() + 50; // 50ms
    db.set("expkey", "val", exp);
    EXPECT_TRUE(db.get("expkey").has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(db.get("expkey").has_value());
    db.close();
}

TEST(DB, Incr) {
    auto dir = tmp_dir("incr");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    EXPECT_EQ(db.incr("counter", 1), 1LL);
    EXPECT_EQ(db.incr("counter", 1), 2LL);
    EXPECT_EQ(db.incr("counter", -1), 1LL);
    db.close();
}

TEST(DB, IncrBy) {
    auto dir = tmp_dir("incrby");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    EXPECT_EQ(db.incr("n", 10), 10LL);
    EXPECT_EQ(db.incr("n", 5),  15LL);
    EXPECT_EQ(db.incr("n", -3), 12LL);
    db.close();
}

TEST(DB, Keys) {
    auto dir = tmp_dir("keys");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    db.set("aaa", "1");
    db.set("aab", "2");
    db.set("bbb", "3");
    auto ks = db.keys("a*");
    EXPECT_EQ(ks.size(), 2u);
    auto all = db.keys("*");
    EXPECT_EQ(all.size(), 3u);
    db.close();
}

TEST(DB, Append) {
    auto dir = tmp_dir("append");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    EXPECT_EQ(db.append("str", "Hello"),  5u);
    EXPECT_EQ(db.append("str", " World"), 11u);
    auto v = db.get("str");
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, "Hello World");
    db.close();
}

TEST(DB, Strlen) {
    auto dir = tmp_dir("strlen");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    EXPECT_EQ(db.strlen("missing"), 0u);
    db.set("key", "hello");
    EXPECT_EQ(db.strlen("key"), 5u);
    db.close();
}

TEST(DB, Persistence) {
    // Write data, close, reopen, verify data survived WAL replay
    auto dir = tmp_dir("persist");
    {
        DB db(dir, "no", 64*1024*1024);
        db.open();
        db.set("persistent_key", "hello_world");
        db.close();
    }
    {
        DB db(dir, "no", 64*1024*1024);
        db.open();
        auto v = db.get("persistent_key");
        EXPECT_TRUE(v.has_value());
        EXPECT_EQ(*v, "hello_world");
        db.close();
    }
}

TEST(DB, FlushAll) {
    auto dir = tmp_dir("flush");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    db.set("a", "1");
    db.set("b", "2");
    db.flush_all();
    EXPECT_FALSE(db.get("a").has_value());
    EXPECT_FALSE(db.get("b").has_value());
    EXPECT_EQ(db.dbsize(), 0u);
    db.close();
}

TEST(DB, Exists) {
    auto dir = tmp_dir("exists");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    EXPECT_FALSE(db.exists("x"));
    db.set("x", "y");
    EXPECT_TRUE(db.exists("x"));
    db.del("x");
    EXPECT_FALSE(db.exists("x"));
    db.close();
}

TEST(DB, Ttl) {
    auto dir = tmp_dir("ttl");
    DB db(dir, "no", 64*1024*1024);
    db.open();
    EXPECT_EQ(db.ttl_ms("missing"), -2LL);
    db.set("k", "v");
    EXPECT_EQ(db.ttl_ms("k"), -1LL);  // no expiry
    using namespace std::chrono;
    int64_t exp = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count() + 5000;
    db.set("k2", "v2", exp);
    EXPECT_GE(db.ttl_ms("k2"), 4000LL);
    db.close();
}
