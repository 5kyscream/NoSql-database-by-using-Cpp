#pragma once
#include <list>
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <cstddef>

namespace minired::storage::cache {

// ── LRU block cache ───────────────────────────────────────────────────────────
// Caches (SSTable_path + ":" + key) → value strings.
// Evicts least-recently-used entries when capacity_bytes is exceeded.
class LRUCache {
public:
    explicit LRUCache(size_t capacity_bytes);

    void put(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    void invalidate(const std::string& key);
    void clear();

    size_t size_bytes()  const { return size_bytes_; }
    size_t entry_count() const { return map_.size(); }

private:
    size_t capacity_bytes_;
    size_t size_bytes_ = 0;

    // LRU list: most recently used at front.
    using ListEntry = std::pair<std::string, std::string>;  // {key, value}
    std::list<ListEntry> list_;
    std::unordered_map<std::string, std::list<ListEntry>::iterator> map_;
    mutable std::mutex mu_;

    void evict_one();
};

} // namespace minired::storage::cache
