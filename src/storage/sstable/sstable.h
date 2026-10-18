#pragma once
#include "storage/db.h"
#include "storage/bloom/bloom_filter.h"
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <memory>

namespace minired::storage::sstable {

// ── SSTable record ────────────────────────────────────────────────────────────
struct Record {
    std::string key;
    std::string value;
    int64_t     expiry_ms = 0;
    bool        tombstone = false;
};

// ── Sparse index entry ────────────────────────────────────────────────────────
struct IndexEntry {
    std::string key;
    uint64_t    file_offset = 0;
};

// ── Footer (at end of file) ───────────────────────────────────────────────────
struct Footer {
    uint64_t index_offset  = 0;
    uint32_t index_count   = 0;
    uint64_t bloom_offset  = 0;
    uint32_t bloom_size    = 0;
    // Magic constant at very end
    static constexpr uint32_t kMagic = 0x4D524544; // 'MRED'
};

// ─────────────────────────────────────────────────────────────────────────────
// SSTableWriter: writes sorted records to a new .sst file.
// ─────────────────────────────────────────────────────────────────────────────
class SSTableWriter {
public:
    // index_interval: write an index entry every N records.
    SSTableWriter(const std::string& path, int index_interval = 16);
    void add(const Record& rec);
    void finish();   // write index, bloom, footer; close file.
    ~SSTableWriter();

private:
    std::string   path_;
    int           index_interval_;
    std::ofstream out_;
    int64_t       record_count_ = 0;
    bloom::BloomFilter bloom_;
    std::vector<IndexEntry> index_;

    static void write_u32(std::ofstream& f, uint32_t v);
    static void write_u64(std::ofstream& f, uint64_t v);
    static void write_str(std::ofstream& f, const std::string& s);
    uint64_t current_pos();
};

// ─────────────────────────────────────────────────────────────────────────────
// SSTableReader: reads an .sst file; supports bloom + binary index seek.
// ─────────────────────────────────────────────────────────────────────────────
class SSTableReader {
public:
    explicit SSTableReader(const std::string& path);
    bool open();      // returns false if file corrupt/missing
    void close();

    // Returns nullopt if definitely absent (bloom) or not found.
    std::optional<Record> get(const std::string& key);

    const std::string& path() const { return path_; }

private:
    std::string    path_;
    std::ifstream  in_;
    Footer         footer_;
    std::vector<IndexEntry> index_;
    bloom::BloomFilter bloom_;
    bool           valid_ = false;

    bool load_footer();
    bool load_index();
    bool load_bloom();

    // Seek to offset, scan forward for key; returns nullopt if not found before next index.
    std::optional<Record> scan_from(uint64_t offset, const std::string& key);

    static uint32_t     read_u32(std::ifstream& f);
    static uint64_t     read_u64(std::ifstream& f);
    static std::string  read_str(std::ifstream& f);
    static Record       read_record(std::ifstream& f);
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers to generate SSTable file paths.
// ─────────────────────────────────────────────────────────────────────────────
std::string make_sst_path(const std::string& data_dir, uint64_t seq);

} // namespace minired::storage::sstable
