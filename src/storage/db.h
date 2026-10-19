#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <memory>

// Forward-declare LSM sub-systems (full includes only in db.cpp)
namespace minired::storage::wal      { class WAL; }
namespace minired::storage::memtable { class Memtable; }
namespace minired::storage::sstable  { class SSTableReader; }
namespace minired::storage::cache    { class LRUCache; }

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
// Backed by a full LSM engine:
//   Write path: WAL → Memtable → (flush) → SSTable → (compact)
//   Read path:  Memtable → SSTable chain (bloom + sparse index) → LRU cache
class DB {
public:
    explicit DB(const std::string& data_dir, const std::string& fsync_policy,
                size_t memtable_max_bytes,
                size_t block_cache_bytes   = 8  * 1024 * 1024,
                int    compaction_trigger  = 4,
                int    index_interval      = 16);
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

    // ── Integer helpers (INCR/DECR/INCRBY/DECRBY) ───────────────────────────
    // Returns new value or throws if not integer.
    int64_t incr(const std::string& key, int64_t delta = 1);

    // ── String helpers (APPEND / STRLEN) ────────────────────────────────────
    size_t  append(const std::string& key, const std::string& suffix); // returns new length
    size_t  strlen(const std::string& key);

    // ── Metrics (for INFO) ───────────────────────────────────────────────────
    uint64_t total_commands_processed() const { return cmds_processed_.load(); }
    void     count_command() { ++cmds_processed_; }
    size_t   memtable_size_bytes() const;
    int      sstable_count() const;
    uint64_t bloom_skips() const;
    uint64_t cache_hits() const;
    uint64_t cache_misses() const;

private:
    // ── Config ───────────────────────────────────────────────────────────────
    std::string  data_dir_;
    std::string  fsync_policy_;
    size_t       memtable_max_bytes_;
    size_t       block_cache_bytes_;
    int          compaction_trigger_;
    int          index_interval_;

    // ── LSM components ───────────────────────────────────────────────────────
    std::unique_ptr<wal::WAL>           wal_;
    std::unique_ptr<memtable::Memtable> memtable_;
    // SSTable readers: index 0 = newest file (highest seq), last = oldest
    std::vector<std::unique_ptr<sstable::SSTableReader>> sst_readers_;
    std::unique_ptr<cache::LRUCache>    block_cache_;

    uint64_t next_sst_seq_ = 0;   // monotonically increasing SST file number

    mutable std::mutex mu_;        // protects memtable_ + sst_readers_

    // ── Metrics ──────────────────────────────────────────────────────────────
    std::atomic<uint64_t> cmds_processed_{0};
    std::atomic<uint64_t> bloom_skips_{0};
    std::atomic<uint64_t> cache_hits_{0};
    std::atomic<uint64_t> cache_misses_{0};

    // ── Internal helpers ─────────────────────────────────────────────────────
    static int64_t now_ms();
    bool glob_match(const std::string& pattern, const std::string& str) const;

    // Write key→value (or tombstone) to WAL then memtable.
    // Caller must hold mu_.
    void lsm_write(const std::string& key, const std::string& value,
                   int64_t expiry_ms, bool tombstone);

    // Flush memtable to a new SSTable; truncate WAL; maybe compact.
    // Caller must hold mu_.
    void flush_memtable();

    // Compact all SSTables into one when count >= trigger.
    // Caller must hold mu_.
    void maybe_compact();

    // Scan all SSTables for key (newest first, bloom-filtered, LRU cached).
    std::optional<Entry> get_from_sst(const std::string& key);

    // Reload SSTable readers from disk after compaction or startup.
    // Caller must hold mu_.
    void reload_sst_readers();

    // RESP2-encode a SET command for WAL logging.
    static std::string wal_encode_set(const std::string& key,
                                      const std::string& value,
                                      int64_t expiry_ms);
    static std::string wal_encode_del(const std::string& key);
};

} // namespace minired::storage
