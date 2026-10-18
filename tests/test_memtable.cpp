// test_memtable.cpp — Memtable unit tests (Phase 3 – stub for Phase 1)
#include "gtest_compat.h"
#include "storage/db.h"
#include <thread>
#include <chrono>

using namespace minired::storage;

TEST(Memtable, SetGet) {
    DB db("data_test", "no", 64*1024*1024);
    db.open();
    db.set("foo", "bar");
    auto v = db.get("foo");
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, "bar");
    db.close();
}

TEST(Memtable, GetMissing) {
    DB db("data_test", "no", 64*1024*1024);
    db.open();
    EXPECT_FALSE(db.get("nosuchkey").has_value());
    db.close();
}

TEST(Memtable, Delete) {
    DB db("data_test", "no", 64*1024*1024);
    db.open();
    db.set("k", "v");
    EXPECT_TRUE(db.del("k"));
    EXPECT_FALSE(db.get("k").has_value());
    EXPECT_FALSE(db.del("k"));
    db.close();
}

TEST(Memtable, Expiry) {
    DB db("data_test", "no", 64*1024*1024);
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

TEST(Memtable, Incr) {
    DB db("data_test", "no", 64*1024*1024);
    db.open();
    EXPECT_EQ(db.incr("counter", 1), 1LL);
    EXPECT_EQ(db.incr("counter", 1), 2LL);
    EXPECT_EQ(db.incr("counter", -1), 1LL);
    db.close();
}

TEST(Memtable, KEYS) {
    DB db("data_test", "no", 64*1024*1024);
    db.open();
    db.flush_all();
    db.set("aaa", "1");
    db.set("aab", "2");
    db.set("bbb", "3");
    auto ks = db.keys("a*");
    EXPECT_EQ(ks.size(), 2u);
    db.close();
}
