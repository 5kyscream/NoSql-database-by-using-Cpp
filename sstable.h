// sstable.h
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <cstdint>
#include "memtable.h"

// ── File format constants ────────────────────────────────────
static constexpr uint32_t SSTABLE_MAGIC   = 0x4C534D54; // "LSMT"
static constexpr uint32_t SSTABLE_VERSION = 1;

// ── On-disk record layout ────────────────────────────────────
// [ key_len: 4 bytes ][ key: key_len bytes ]
// [ val_len: 4 bytes ][ val: val_len bytes ]
// [ flags:   1 byte  ]   0 = normal, 1 = tombstone

// ── On-disk header layout ────────────────────────────────────
// [ magic:       4 bytes ]
// [ version:     4 bytes ]
// [ num_entries: 4 bytes ]

struct SSTableRecord {
    std::string key;
    std::string value;
    bool is_tombstone = false;
};

// ────────────────────────────────────────────────────────────
//  WRITER — takes a sealed MemTable, writes SSTable to disk
// ────────────────────────────────────────────────────────────
class SSTableWriter {
public:
    // Writes all entries from memtable to filepath in sorted order
    static void write(const std::string& filepath,
                      const MemTable& memtable) {

        std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath);
        }

        const auto& entries = memtable.get_sorted_entries();

        // ── Write header ─────────────────────────────────────
        uint32_t num_entries = static_cast<uint32_t>(entries.size());
        write_uint32(file, SSTABLE_MAGIC);
        write_uint32(file, SSTABLE_VERSION);
        write_uint32(file, num_entries);

        // ── Write data block (sorted by key) ─────────────────
        for (const auto& [key, entry] : entries) {
            write_record(file, key, entry);
        }

        file.flush();
        file.close();

        std::cout << "[SSTableWriter] Wrote " << num_entries
                  << " entries to " << filepath << "\n";
    }

private:
    static void write_uint32(std::ofstream& f, uint32_t val) {
        f.write(reinterpret_cast<const char*>(&val), sizeof(val));
    }

    static void write_bytes(std::ofstream& f, const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        write_uint32(f, len);                          // length prefix
        f.write(s.data(), len);                        // raw bytes
    }

    static void write_record(std::ofstream& f,
                              const std::string& key,
                              const Entry& entry) {
        write_bytes(f, key);                           // key_len + key
        write_bytes(f, entry.value);                   // val_len + val
        uint8_t flag = entry.is_tombstone ? 1 : 0;
        f.write(reinterpret_cast<const char*>(&flag), 1); // flags
    }
};

// ────────────────────────────────────────────────────────────
//  READER — loads all records from an SSTable file
// ────────────────────────────────────────────────────────────
class SSTableReader {
public:
    explicit SSTableReader(const std::string& filepath)
        : filepath_(filepath) {
        load();
    }

    // Linear scan for a key — we'll replace this with
    // binary search in Module 9
    std::optional<SSTableRecord> get(const std::string& key) const {
        for (const auto& record : records_) {
            if (record.key == key) return record;
        }
        return std::nullopt;
    }

    const std::vector<SSTableRecord>& records() const {
        return records_;
    }

    void print() const {
        std::cout << "\n=== SSTable: " << filepath_ << " ===\n";
        std::cout << "Entries: " << records_.size() << "\n";
        for (const auto& r : records_) {
            if (r.is_tombstone) {
                std::cout << "  " << r.key << " → <TOMBSTONE>\n";
            } else {
                std::cout << "  " << r.key << " → " << r.value << "\n";
            }
        }
        std::cout << "========================\n";
    }

private:
    std::string filepath_;
    std::vector<SSTableRecord> records_;

    void load() {
        std::ifstream file(filepath_, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath_);
        }

        // ── Validate header ───────────────────────────────────
        uint32_t magic   = read_uint32(file);
        uint32_t version = read_uint32(file);
        uint32_t count   = read_uint32(file);

        if (magic != SSTABLE_MAGIC) {
            throw std::runtime_error("Invalid SSTable: bad magic number");
        }
        if (version != SSTABLE_VERSION) {
            throw std::runtime_error("Unsupported SSTable version");
        }

        // ── Read data block ───────────────────────────────────
        records_.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            records_.push_back(read_record(file));
        }

        std::cout << "[SSTableReader] Loaded " << count
                  << " entries from " << filepath_ << "\n";
    }

    static uint32_t read_uint32(std::ifstream& f) {
        uint32_t val = 0;
        f.read(reinterpret_cast<char*>(&val), sizeof(val));
        return val;
    }

    static std::string read_bytes(std::ifstream& f) {
        uint32_t len = read_uint32(f);
        std::string s(len, '\0');
        f.read(s.data(), len);
        return s;
    }

    static SSTableRecord read_record(std::ifstream& f) {
        SSTableRecord r;
        r.key   = read_bytes(f);     // read key_len then key
        r.value = read_bytes(f);     // read val_len then val
        uint8_t flag = 0;
        f.read(reinterpret_cast<char*>(&flag), 1);
        r.is_tombstone = (flag == 1);
        return r;
    }
};