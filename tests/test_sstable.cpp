// test_sstable.cpp — SSTable read/write unit tests
#include "gtest_compat.h"
#include "storage/sstable/sstable.h"
#include <filesystem>
#include <string>
#include <vector>

using namespace minired::storage::sstable;
namespace fs = std::filesystem;

static std::string test_dir(const std::string& name) {
    std::string dir = "./tmp_sst_test_" + name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

TEST(SSTable, WriteAndReadSimple) {
    auto dir = test_dir("simple");
    std::string sst_path = dir + "/00000001.sst";

    {
        SSTableWriter writer(sst_path, 4);
        writer.add(Record{"apple", "10", 0, false});
        writer.add(Record{"banana", "20", 0, false});
        writer.add(Record{"cherry", "30", 0, false});
        writer.finish();
    }

    SSTableReader reader(sst_path);
    EXPECT_TRUE(reader.open());

    auto rec_a = reader.get("apple");
    EXPECT_TRUE(rec_a.has_value());
    EXPECT_EQ(rec_a->value, "10");

    auto rec_b = reader.get("banana");
    EXPECT_TRUE(rec_b.has_value());
    EXPECT_EQ(rec_b->value, "20");

    auto rec_c = reader.get("cherry");
    EXPECT_TRUE(rec_c.has_value());
    EXPECT_EQ(rec_c->value, "30");

    EXPECT_FALSE(reader.get("date").has_value());
    EXPECT_FALSE(reader.get("aaa").has_value());

    reader.close();
    fs::remove_all(dir);
}

TEST(SSTable, SparseIndexAndLargeKeys) {
    auto dir = test_dir("sparse");
    std::string sst_path = dir + "/00000002.sst";

    const int N = 200;
    {
        SSTableWriter writer(sst_path, 8); // index every 8 records
        for (int i = 0; i < N; ++i) {
            char key_buf[16];
            snprintf(key_buf, sizeof(key_buf), "k_%04d", i);
            writer.add(Record{key_buf, "val_" + std::to_string(i), 0, false});
        }
        writer.finish();
    }

    SSTableReader reader(sst_path);
    EXPECT_TRUE(reader.open());

    // Verify all keys can be queried via sparse index binary search
    for (int i = 0; i < N; ++i) {
        char key_buf[16];
        snprintf(key_buf, sizeof(key_buf), "k_%04d", i);
        auto rec = reader.get(key_buf);
        EXPECT_TRUE(rec.has_value());
        EXPECT_EQ(rec->value, "val_" + std::to_string(i));
    }

    EXPECT_FALSE(reader.get("k_9999").has_value());
    EXPECT_FALSE(reader.get("other").has_value());

    reader.close();
    fs::remove_all(dir);
}

TEST(SSTable, TombstoneAndExpiry) {
    auto dir = test_dir("tombstone");
    std::string sst_path = dir + "/00000003.sst";

    {
        SSTableWriter writer(sst_path, 16);
        writer.add(Record{"deleted_key", "", 0, true});
        writer.add(Record{"expiring_key", "val", 123456789LL, false});
        writer.finish();
    }

    SSTableReader reader(sst_path);
    EXPECT_TRUE(reader.open());

    auto rec_del = reader.get("deleted_key");
    EXPECT_TRUE(rec_del.has_value());
    EXPECT_TRUE(rec_del->tombstone);

    auto rec_exp = reader.get("expiring_key");
    EXPECT_TRUE(rec_exp.has_value());
    EXPECT_EQ(rec_exp->expiry_ms, 123456789LL);
    EXPECT_FALSE(rec_exp->tombstone);

    reader.close();
    fs::remove_all(dir);
}
