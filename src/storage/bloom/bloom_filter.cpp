#include "storage/bloom/bloom_filter.h"
#include <stdexcept>
#include <cstring>

namespace minired::storage::bloom {

// ─────────────────────────────────────────────────────────────────────────────
// FNV-1a 64-bit (h1) and a variation (h2) for double hashing.
// ─────────────────────────────────────────────────────────────────────────────
uint64_t BloomFilter::hash1(const std::string& key) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t BloomFilter::hash2(const std::string& key) {
    // djb2 variant
    uint64_t h = 5381;
    for (unsigned char c : key) {
        h = ((h << 5) + h) + c;
    }
    return h | 1;   // ensure odd so gcd(h2,m) is 1 for any power-of-2 m
}

// ─────────────────────────────────────────────────────────────────────────────
void BloomFilter::init(size_t m, int k) {
    m_    = m;
    k_    = k;
    bits_.assign((m + 7) / 8, 0);
}

void BloomFilter::set_bit(size_t pos) {
    bits_[pos / 8] |= static_cast<uint8_t>(1u << (pos % 8));
}

bool BloomFilter::get_bit(size_t pos) const {
    return (bits_[pos / 8] >> (pos % 8)) & 1;
}

void BloomFilter::add(const std::string& key) {
    if (m_ == 0) return;
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    for (int i = 0; i < k_; ++i) {
        size_t pos = (h1 + static_cast<uint64_t>(i) * h2) % m_;
        set_bit(pos);
    }
}

bool BloomFilter::might_contain(const std::string& key) const {
    if (m_ == 0) return true;  // uninitialized → assume present
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    for (int i = 0; i < k_; ++i) {
        size_t pos = (h1 + static_cast<uint64_t>(i) * h2) % m_;
        if (!get_bit(pos)) return false;
    }
    return true;
}

// ── Serialization ─────────────────────────────────────────────────────────────
// Format: [8B m][4B k][bits... ceil(m/8) bytes]
std::vector<uint8_t> BloomFilter::serialize() const {
    std::vector<uint8_t> out;
    out.reserve(12 + bits_.size());
    // m (8 bytes LE)
    for (int i = 0; i < 8; ++i) out.push_back((m_ >> (8*i)) & 0xFF);
    // k (4 bytes LE)
    for (int i = 0; i < 4; ++i) out.push_back((static_cast<uint32_t>(k_) >> (8*i)) & 0xFF);
    // bits
    out.insert(out.end(), bits_.begin(), bits_.end());
    return out;
}

bool BloomFilter::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 12) return false;
    m_ = 0;
    for (int i = 0; i < 8; ++i) m_ |= (static_cast<uint64_t>(data[i]) << (8*i));
    uint32_t k32 = 0;
    for (int i = 0; i < 4; ++i) k32 |= (static_cast<uint32_t>(data[8+i]) << (8*i));
    k_ = static_cast<int>(k32);
    bits_.assign(data.begin() + 12, data.end());
    return bits_.size() == (m_ + 7) / 8;
}

} // namespace minired::storage::bloom
