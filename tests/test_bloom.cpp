// test_bloom.cpp — Bloom filter unit tests
#include "gtest_compat.h"
#include "storage/bloom/bloom_filter.h"
#include <string>
#include <vector>

using namespace minired::storage::bloom;

TEST(BloomFilter, BasicAddAndQuery) {
    BloomFilter bf;
    bf.init(1000, 7);
    EXPECT_TRUE(bf.initialized());

    bf.add("apple");
    bf.add("banana");
    bf.add("cherry");

    EXPECT_TRUE(bf.might_contain("apple"));
    EXPECT_TRUE(bf.might_contain("banana"));
    EXPECT_TRUE(bf.might_contain("cherry"));
    EXPECT_FALSE(bf.might_contain("non_existent_fruit_xyz_123"));
}

TEST(BloomFilter, FalsePositiveRate) {
    BloomFilter bf;
    const size_t n = 10000;
    // For ~1% target FPR, m ≈ 10 * n bits, k = 7
    bf.init(n * 10, 7);

    for (size_t i = 0; i < n; ++i) {
        bf.add("key_" + std::to_string(i));
    }

    // All inserted keys must be reported as present (zero false negatives)
    for (size_t i = 0; i < n; ++i) {
        EXPECT_TRUE(bf.might_contain("key_" + std::to_string(i)));
    }

    // Test false positives on disjoint keys
    size_t fp_count = 0;
    const size_t test_queries = 10000;
    for (size_t i = 0; i < test_queries; ++i) {
        if (bf.might_contain("absent_key_" + std::to_string(i))) {
            fp_count++;
        }
    }

    double fp_rate = static_cast<double>(fp_count) / test_queries;
    // Expected FPR is ~1-1.5%; verify it is strictly under 2.5%
    EXPECT_LE(fp_rate, 0.025);
}

TEST(BloomFilter, SerializeDeserialize) {
    BloomFilter bf1;
    bf1.init(5000, 7);
    bf1.add("persist_1");
    bf1.add("persist_2");

    auto bytes = bf1.serialize();
    EXPECT_FALSE(bytes.empty());

    BloomFilter bf2;
    EXPECT_TRUE(bf2.deserialize(bytes));
    EXPECT_TRUE(bf2.might_contain("persist_1"));
    EXPECT_TRUE(bf2.might_contain("persist_2"));
    EXPECT_FALSE(bf2.might_contain("persist_3"));
}
