// test_compaction.cpp — Compaction unit tests
#include "gtest_compat.h"
#include "storage/compaction/compaction.h"
#include "storage/sstable/sstable.h"
#include <filesystem>
#include <string>
#include <vector>

using namespace minired::storage::compaction;
using namespace minired::storage::sstable;
namespace fs = std::filesystem;

static std::string test_dir(const std::string& name) {
    std::string dir = "./tmp_compact_test_" + name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

TEST(Compaction, MergeOverwritingAndTombstones) {
    auto dir = test_dir("merge");
    std::string sst_old = dir + "/00000001.sst";
    std::string sst_new = dir + "/00000002.sst";
    std::string sst_out = dir + "/00000003.sst";

    // Older SSTable has:
    // "a" -> "old_a"
    // "b" -> "old_b"
    // "c" -> "old_c"
    {
        SSTableWriter w(sst_old, 4);
        w.add(Record{"a", "old_a", 0, false});
        w.add(Record{"b", "old_b", 0, false});
        w.add(Record{"c", "old_c", 0, false});
        w.finish();
    }

    // Newer SSTable has:
    // "b" -> "new_b" (overwritten)
    // "c" -> tombstone (deleted)
    // "d" -> "new_d" (inserted)
    {
        SSTableWriter w(sst_new, 4);
        w.add(Record{"b", "new_b", 0, false});
        w.add(Record{"c", "", 0, true});
        w.add(Record{"d", "new_d", 0, false});
        w.finish();
    }

    // Compactor expects newest first: [sst_new, sst_old]
    bool ok = Compactor::compact({sst_new, sst_old}, sst_out, 4);
    EXPECT_TRUE(ok);

    SSTableReader reader(sst_out);
    EXPECT_TRUE(reader.open());

    // "a" should be retained from older sstable
    auto rec_a = reader.get("a");
    EXPECT_TRUE(rec_a.has_value());
    EXPECT_EQ(rec_a->value, "old_a");

    // "b" should have value from newer sstable
    auto rec_b = reader.get("b");
    EXPECT_TRUE(rec_b.has_value());
    EXPECT_EQ(rec_b->value, "new_b");

    // "c" had tombstone in newer, so dropped entirely in full compaction
    EXPECT_FALSE(reader.get("c").has_value());

    // "d" should exist
    auto rec_d = reader.get("d");
    EXPECT_TRUE(rec_d.has_value());
    EXPECT_EQ(rec_d->value, "new_d");

    reader.close();
    fs::remove_all(dir);
}
