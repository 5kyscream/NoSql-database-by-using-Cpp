#pragma once
#include <bitset>
#include <string>
#include <vector>
#include <cstdint>

namespace minired::storage::bloom {

// ── Bloom filter with double hashing ─────────────────────────────────────────
// m = bit count, k = hash functions
// For 1% FPR at n keys: m ≈ 9.6*n, k ≈ 6.6
// We use a dynamic bitset (std::vector<uint8_t>) to support variable m.
class BloomFilter {
public:
    // Construct empty filter (will be sized when first add() is called or
    // explicitly via init()).
    BloomFilter() = default;

    // Initialize with target bit count and hash count.
    // Typical usage: init(n_keys * 10, 7)
    void init(size_t m, int k);

    void add(const std::string& key);
    bool might_contain(const std::string& key) const;

    // Serialization (for SSTable footer)
    std::vector<uint8_t> serialize() const;
    bool deserialize(const std::vector<uint8_t>& data);

    size_t bit_count()  const { return m_; }
    int    hash_count() const { return k_; }
    bool   initialized() const { return m_ > 0; }

private:
    size_t                m_ = 0;   // number of bits
    int                   k_ = 0;   // number of hash functions
    std::vector<uint8_t>  bits_;    // packed bit array (ceil(m/8) bytes)

    void set_bit(size_t pos);
    bool get_bit(size_t pos) const;

    // Double hashing: h_i(x) = (h1(x) + i * h2(x)) % m
    static uint64_t hash1(const std::string& key);
    static uint64_t hash2(const std::string& key);
};

} // namespace minired::storage::bloom
