#include "storage/cache/lru_cache.h"

namespace minired::storage::cache {

LRUCache::LRUCache(size_t capacity_bytes) : capacity_bytes_(capacity_bytes) {}

void LRUCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        // Update existing: remove old, reinsert at front
        size_bytes_ -= it->second->first.size() + it->second->second.size();
        list_.erase(it->second);
        map_.erase(it);
    }
    list_.push_front({key, value});
    map_[key] = list_.begin();
    size_bytes_ += key.size() + value.size();

    while (size_bytes_ > capacity_bytes_ && !list_.empty()) {
        evict_one();
    }
}

std::optional<std::string> LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return std::nullopt;
    // Move to front (most recently used)
    list_.splice(list_.begin(), list_, it->second);
    return it->second->second;
}

void LRUCache::invalidate(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return;
    size_bytes_ -= it->second->first.size() + it->second->second.size();
    list_.erase(it->second);
    map_.erase(it);
}

void LRUCache::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    list_.clear();
    map_.clear();
    size_bytes_ = 0;
}

void LRUCache::evict_one() {
    if (list_.empty()) return;
    auto& last = list_.back();
    size_bytes_ -= last.first.size() + last.second.size();
    map_.erase(last.first);
    list_.pop_back();
}

} // namespace minired::storage::cache
