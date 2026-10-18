#pragma once
#include "storage/db.h"
#include <map>
#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace minired::storage::memtable {

// ── Memtable: sorted in-memory store (std::map for ordered iteration) ────────
// When size_bytes() >= threshold, the DB flushes this to an SSTable.
class Memtable {
public:
    void   put(const std::string& key, const std::string& value,
               int64_t expiry_ms = 0);
    void   put_tombstone(const std::string& key);

    std::optional<Entry> get(const std::string& key) const;

    // For SSTable flush: iterate in key order
    using Iterator = std::map<std::string, Entry>::const_iterator;
    Iterator begin() const { return data_.begin(); }
    Iterator end()   const { return data_.end();   }

    size_t size()       const { return data_.size(); }
    size_t size_bytes() const { return size_bytes_; }
    bool   empty()      const { return data_.empty(); }
    void   clear();

private:
    std::map<std::string, Entry> data_;
    size_t size_bytes_ = 0;

    void account(const std::string& key, const std::string& val) {
        size_bytes_ += key.size() + val.size() + 16;
    }
};

} // namespace minired::storage::memtable
