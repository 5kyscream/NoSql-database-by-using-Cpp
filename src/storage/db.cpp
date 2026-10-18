#include "storage/db.h"
#include <chrono>
#include <stdexcept>
#include <regex>
#include <algorithm>
#include <unordered_map>

namespace minired::storage {

// ─────────────────────────────────────────────────────────────────────────────
DB::DB(const std::string& data_dir, const std::string& fsync_policy,
       size_t memtable_max_bytes)
    : data_dir_(data_dir)
    , fsync_policy_(fsync_policy)
    , memtable_max_bytes_(memtable_max_bytes)
{}

DB::~DB() { close(); }

void DB::open()  { /* Phase 2+: replay WAL, load SSTables */ }
void DB::close() { /* Phase 2+: flush memtable, close WAL  */ }

// ─────────────────────────────────────────────────────────────────────────────
int64_t DB::now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

// ─────────────────────────────────────────────────────────────────────────────
// Glob matcher: only '*' wildcard (Redis KEYS only needs this).
// Extended to support '?' and '[' in Phase 4 if needed.
// ─────────────────────────────────────────────────────────────────────────────
bool DB::glob_match(const std::string& pat, const std::string& str) const {
    // Simple recursive glob: '*' matches any sequence.
    size_t pi = 0, si = 0;
    size_t star_pi = std::string::npos, star_si = 0;
    while (si < str.size()) {
        if (pi < pat.size() && (pat[pi] == '?' || pat[pi] == str[si])) {
            ++pi; ++si;
        } else if (pi < pat.size() && pat[pi] == '*') {
            star_pi = pi++;
            star_si = si;
        } else if (star_pi != std::string::npos) {
            pi = star_pi + 1;
            si = ++star_si;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '*') ++pi;
    return pi == pat.size();
}

// ─────────────────────────────────────────────────────────────────────────────
void DB::set(const std::string& key, const std::string& value, int64_t expiry_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    store_[key] = Entry{value, expiry_ms, false};
}

std::optional<std::string> DB::get(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = store_.find(key);
    if (it == store_.end()) return std::nullopt;
    if (it->second.is_expired(now_ms())) {
        store_.erase(it);
        return std::nullopt;
    }
    return it->second.value;
}

bool DB::del(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = store_.find(key);
    if (it == store_.end()) return false;
    store_.erase(it);
    return true;
}

bool DB::exists(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = store_.find(key);
    if (it == store_.end()) return false;
    if (it->second.is_expired(now_ms())) {
        store_.erase(it);
        return false;
    }
    return true;
}

bool DB::expire(const std::string& key, int64_t expiry_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = store_.find(key);
    if (it == store_.end()) return false;
    if (it->second.is_expired(now_ms())) {
        store_.erase(it);
        return false;
    }
    it->second.expiry_ms = expiry_ms;
    return true;
}

int64_t DB::ttl_ms(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = store_.find(key);
    if (it == store_.end()) return -2;
    auto& e = it->second;
    int64_t now = now_ms();
    if (e.is_expired(now)) {
        store_.erase(it);
        return -2;
    }
    if (e.expiry_ms == 0) return -1;
    return e.expiry_ms - now;
}

std::vector<std::string> DB::keys(const std::string& pattern) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();
    std::vector<std::string> result;
    for (auto& [k, v] : store_) {
        if (!v.is_expired(now) && glob_match(pattern, k)) {
            result.push_back(k);
        }
    }
    return result;
}

size_t DB::dbsize() {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();
    size_t count = 0;
    for (auto& [k, v] : store_) {
        if (!v.is_expired(now)) ++count;
    }
    return count;
}

void DB::flush_all() {
    std::lock_guard<std::mutex> lk(mu_);
    store_.clear();
}

int64_t DB::incr(const std::string& key, int64_t delta) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();
    auto it = store_.find(key);
    if (it == store_.end() || it->second.is_expired(now)) {
        store_[key] = Entry{std::to_string(delta), 0, false};
        return delta;
    }
    int64_t cur;
    try {
        cur = std::stoll(it->second.value);
    } catch (...) {
        throw std::runtime_error("ERR value is not an integer or out of range");
    }
    cur += delta;
    it->second.value = std::to_string(cur);
    return cur;
}

// ── Metric stubs (Phase 3 will fill these in) ────────────────────────────────
size_t DB::memtable_size_bytes() const { return store_.size() * 64; } // rough estimate
int    DB::sstable_count()       const { return 0; }
uint64_t DB::bloom_skips()       const { return bloom_skips_.load(); }
uint64_t DB::cache_hits()        const { return cache_hits_.load(); }
uint64_t DB::cache_misses()      const { return cache_misses_.load(); }

} // namespace minired::storage
