// memtable.h
#pragma once
#include <map>
#include <string>
#include <optional>
#include <stdexcept>
#include <iostream>

// Every entry in the MemTable is either a real value or a tombstone
struct Entry {
    std::string value;
    bool is_tombstone = false;
};

class MemTable {
public:
    // size_limit_bytes: when our data exceeds this, we need to flush
    explicit MemTable(size_t size_limit_bytes = 4 * 1024 * 1024) // 4 MB default
        : size_limit_(size_limit_bytes)
        , current_size_(0)
        , immutable_(false)
    {}

    // ── Writes ──────────────────────────────────────────────────

    void put(const std::string& key, const std::string& value) {
        ensure_writable();

        // If key already exists, subtract its old size first
        auto it = store_.find(key);
        if (it != store_.end()) {
            current_size_ -= entry_size(key, it->second);
        }

        Entry e{value, false};
        store_[key] = e;
        current_size_ += entry_size(key, e);
    }

    void del(const std::string& key) {
        ensure_writable();

        auto it = store_.find(key);
        if (it != store_.end()) {
            current_size_ -= entry_size(key, it->second);
        }

        // Write tombstone — not an erasure
        Entry e{"", true};
        store_[key] = e;
        current_size_ += entry_size(key, e);
    }

    // ── Reads ───────────────────────────────────────────────────

    // Returns:
    //   std::nullopt  → key not in this MemTable at all
    //   Entry with is_tombstone=true → key was deleted
    //   Entry with is_tombstone=false → key is alive, has a value
    std::optional<Entry> get(const std::string& key) const {
        auto it = store_.find(key);
        if (it == store_.end()) return std::nullopt;
        return it->second;
    }

    // ── State ───────────────────────────────────────────────────

    bool needs_flush() const {
        return current_size_ >= size_limit_;
    }

    // Seal this MemTable — no more writes allowed
    // Called before flushing to disk
    void make_immutable() {
        immutable_ = true;
    }

    bool is_immutable() const { return immutable_; }

    size_t size_bytes() const { return current_size_; }

    size_t num_entries() const { return store_.size(); }

    // ── Iteration (used during SSTable flush) ───────────────────

    // Returns all entries in sorted key order
    // This is what gets written to disk as an SSTable
    const std::map<std::string, Entry>& get_sorted_entries() const {
        return store_;
    }

    // ── Debug ───────────────────────────────────────────────────

    void print() const {
        std::cout << "\n=== MemTable ["
                  << (immutable_ ? "IMMUTABLE" : "ACTIVE")
                  << "] ===\n";
        std::cout << "Size: " << current_size_ << " / "
                  << size_limit_ << " bytes\n";
        std::cout << "Entries:\n";

        for (const auto& [key, entry] : store_) {
            if (entry.is_tombstone) {
                std::cout << "  " << key << " → <TOMBSTONE>\n";
            } else {
                std::cout << "  " << key << " → " << entry.value << "\n";
            }
        }
        std::cout << "==================\n";
    }

private:
    std::map<std::string, Entry> store_;
    size_t size_limit_;
    size_t current_size_;
    bool immutable_;

    void ensure_writable() const {
        if (immutable_) {
            throw std::runtime_error(
                "Cannot write to an immutable MemTable"
            );
        }
    }

    // How many bytes does one key+entry consume?
    // In real databases this includes metadata overhead too.
    static size_t entry_size(const std::string& key,
                              const Entry& entry) {
        return key.size() + entry.value.size() + sizeof(bool);
    }
};