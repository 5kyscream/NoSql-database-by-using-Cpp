#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <atomic>
#include <unordered_map>
#include <mutex>

namespace minired::storage {

// ── Value stored per key ─────────────────────────────────────────────────────
struct Entry {
    std::string value;
    int64_t     expiry_ms = 0;  // 0 = no expiry; absolute epoch ms
    bool        tombstone = false;

    bool is_expired(int64_t now_ms) const {
        return expiry_ms > 0 && now_ms >= expiry_ms;
    }
};

// ── DB: the public facade used by command handlers ───────────────────────────
// Phase 1: pure in-memory (std::unordered_map).
// Phase 2+: backed by WAL + LSM engine.
class DB {
public:
    explicit DB(const std::string& data_dir, const std::string& fsync_policy,
                size_t memtable_max_bytes);
    ~DB();

    // Initialise storage (replay WAL, open SSTables, etc.)
    void open();
    void close();

    // ── Core operations ──────────────────────────────────────────────────────
    void   set(const std::string& key, const std::string& value, int64_t expiry_ms = 0);
    std::optional<std::string> get(const std::string& key);
    bool   del(const std::string& key);                 // returns true if key existed
    bool   exists(const std::string& key);
    bool   expire(const std::string& key, int64_t expiry_ms); // false = key not found
    int64_t ttl_ms(const std::string& key);            // -1=no expiry, -2=not found, else ms remaining
    std::vector<std::string> keys(const std::string& pattern);
    size_t dbsize();
    void   flush_all();

    // ── Integer helpers (INCR/DECR) ──────────────────────────────────────────
    // Returns new value or throws if not integer.
    int64_t incr(const std::string& key, int64_t delta = 1);

    // ── Metrics (for INFO) ───────────────────────────────────────────────────
    uint64_t total_commands_processed() const { return cmds_processed_.load(); }
    void     count_command() { ++cmds_processed_; }
    size_t   memtable_size_bytes() const;
    int      sstable_count() const;
    uint64_t bloom_skips() const;
    uint64_t cache_hits() const;
    uint64_t cache_misses() const;

private:
    std::string  data_dir_;
    std::string  fsync_policy_;
    size_t       memtable_max_bytes_;

    // Phase 1: in-memory store (replaced by LSM in Phase 3)
    std::unordered_map<std::string, Entry> store_;
    mutable std::mutex mu_;

    std::atomic<uint64_t> cmds_processed_{0};
    std::atomic<uint64_t> bloom_skips_{0};
    std::atomic<uint64_t> cache_hits_{0};
    std::atomic<uint64_t> cache_misses_{0};

    static int64_t now_ms();
    bool glob_match(const std::string& pattern, const std::string& str) const;
};

} // namespace minired::storage
