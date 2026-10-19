#include "storage/db.h"
#include "storage/wal/wal.h"
#include "storage/memtable/memtable.h"
#include "storage/sstable/sstable.h"
#include "storage/compaction/compaction.h"
#include "storage/cache/lru_cache.h"
#include "protocol/resp_parser.h"

#include <chrono>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace minired::storage {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
DB::DB(const std::string& data_dir, const std::string& fsync_policy,
       size_t memtable_max_bytes,
       size_t block_cache_bytes,
       int    compaction_trigger,
       int    index_interval)
    : data_dir_(data_dir)
    , fsync_policy_(fsync_policy)
    , memtable_max_bytes_(memtable_max_bytes)
    , block_cache_bytes_(block_cache_bytes)
    , compaction_trigger_(compaction_trigger)
    , index_interval_(index_interval)
{
    memtable_  = std::make_unique<memtable::Memtable>();
    block_cache_ = std::make_unique<cache::LRUCache>(block_cache_bytes_);
}

DB::~DB() { close(); }

// ─────────────────────────────────────────────────────────────────────────────
int64_t DB::now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

// ─────────────────────────────────────────────────────────────────────────────
// WAL encoding helpers: produce RESP2 multi-bulk for WAL replay
// ─────────────────────────────────────────────────────────────────────────────
std::string DB::wal_encode_set(const std::string& key,
                                const std::string& value,
                                int64_t expiry_ms)
{
    std::ostringstream ss;
    if (expiry_ms > 0) {
        // encode as: *5\r\n$3\r\nSET\r\n$<klen>\r\n<key>\r\n$<vlen>\r\n<val>\r\n$2\r\nPX\r\n$<N>\r\n<ms>\r\n
        std::string exp_str = std::to_string(expiry_ms);
        ss << "*5\r\n"
           << "$3\r\nSET\r\n"
           << "$" << key.size()   << "\r\n" << key   << "\r\n"
           << "$" << value.size() << "\r\n" << value << "\r\n"
           << "$2\r\nPX\r\n"
           << "$" << exp_str.size() << "\r\n" << exp_str << "\r\n";
    } else {
        ss << "*3\r\n"
           << "$3\r\nSET\r\n"
           << "$" << key.size()   << "\r\n" << key   << "\r\n"
           << "$" << value.size() << "\r\n" << value << "\r\n";
    }
    return ss.str();
}

std::string DB::wal_encode_del(const std::string& key) {
    std::ostringstream ss;
    ss << "*2\r\n"
       << "$3\r\nDEL\r\n"
       << "$" << key.size() << "\r\n" << key << "\r\n";
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Glob matcher: * matches any sequence, ? matches any single char
// ─────────────────────────────────────────────────────────────────────────────
bool DB::glob_match(const std::string& pat, const std::string& str) const {
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
// reload_sst_readers: scan data_dir for *.sst files, open readers newest-first
// ─────────────────────────────────────────────────────────────────────────────
void DB::reload_sst_readers() {
    sst_readers_.clear();
    next_sst_seq_ = 0;

    // Collect all .sst paths
    std::vector<std::string> paths;
    try {
        for (auto& entry : fs::directory_iterator(data_dir_)) {
            if (entry.path().extension() == ".sst") {
                paths.push_back(entry.path().string());
            }
        }
    } catch (...) {
        return;  // data_dir doesn't exist yet
    }

    // Sort lexicographically — our naming "00000001.sst" etc. sorts correctly
    std::sort(paths.begin(), paths.end());

    // Update seq counter
    if (!paths.empty()) {
        // Parse sequence number from the last (newest) file name
        std::string last = fs::path(paths.back()).stem().string();
        try { next_sst_seq_ = std::stoull(last) + 1; } catch (...) {}
    }

    // Open readers newest-first (reverse order)
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
        auto reader = std::make_unique<sstable::SSTableReader>(*it);
        if (reader->open()) {
            sst_readers_.push_back(std::move(reader));
        } else {
            std::cerr << "DB: failed to open SSTable " << *it << "\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// open: load SSTables, replay WAL into memtable
// ─────────────────────────────────────────────────────────────────────────────
void DB::open() {
    fs::create_directories(data_dir_);

    // Open WAL
    std::string wal_path = data_dir_ + "/wal.log";
    wal_ = std::make_unique<wal::WAL>(wal_path, fsync_policy_);

    // Load existing SSTables
    {
        std::lock_guard<std::mutex> lk(mu_);
        reload_sst_readers();
    }

    // Replay WAL into memtable (before opening WAL for append)
    // We parse RESP commands and apply them directly to the memtable.
    {
        wal::WAL replay_wal(data_dir_ + "/wal.log", "no");
        replay_wal.open();

        int64_t now = now_ms();
        replay_wal.replay([&](const std::string& resp_cmd) {
            // Parse the RESP command using feed()/pop_command()
            protocol::RespParser parser;
            parser.feed(resp_cmd.data(), resp_cmd.size());
            if (!parser.has_command()) return;
            auto result = parser.pop_command();
            if (result.empty()) return;

            std::string cmd = result[0];
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

            std::lock_guard<std::mutex> lk(mu_);
            if (cmd == "SET" && result.size() >= 3) {
                int64_t expiry_ms = 0;
                // Parse EX / PX options
                for (size_t i = 3; i + 1 < result.size(); i += 2) {
                    std::string opt = result[i];
                    std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
                    try {
                        int64_t val = std::stoll(result[i + 1]);
                        // WAL stores absolute epoch-ms for PX, and seconds*1000 offset for EX
                        // Since we encode with PX (absolute ms), just use PX.
                        if (opt == "PX")  expiry_ms = val;
                        else if (opt == "EX") expiry_ms = val * 1000 + now; // relative → absolute (approx)
                    } catch (...) {}
                }
                // Only replay if not expired
                if (expiry_ms == 0 || now < expiry_ms) {
                    memtable_->put(result[1], result[2], expiry_ms);
                }
            } else if (cmd == "DEL" && result.size() >= 2) {
                for (size_t i = 1; i < result.size(); ++i)
                    memtable_->put_tombstone(result[i]);
            }
        });
        replay_wal.close();
    }

    // Now open WAL in append mode
    wal_->open();
}

void DB::close() {
    if (!wal_) return;

    // Flush memtable to SSTable if non-empty
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!memtable_->empty()) {
            flush_memtable();
        }
    }

    wal_->close();
    wal_.reset();

    {
        std::lock_guard<std::mutex> lk(mu_);
        sst_readers_.clear();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// flush_memtable: write memtable to SSTable, truncate WAL.  Caller holds mu_.
// ─────────────────────────────────────────────────────────────────────────────
void DB::flush_memtable() {
    if (memtable_->empty()) return;

    std::string sst_path = sstable::make_sst_path(data_dir_, next_sst_seq_);

    try {
        sstable::SSTableWriter writer(sst_path, index_interval_);
        for (auto it = memtable_->begin(); it != memtable_->end(); ++it) {
            sstable::Record rec{
                it->first,
                it->second.value,
                it->second.expiry_ms,
                it->second.tombstone
            };
            writer.add(rec);
        }
        writer.finish();
    } catch (const std::exception& e) {
        std::cerr << "DB: SSTable flush failed: " << e.what() << "\n";
        return;
    }

    ++next_sst_seq_;
    memtable_->clear();

    // Truncate WAL
    if (wal_) wal_->truncate();

    // Reload readers (newest first)
    reload_sst_readers();

    // Maybe compact
    maybe_compact();
}

// ─────────────────────────────────────────────────────────────────────────────
// maybe_compact: k-way merge all SSTables into one if count >= trigger
// ─────────────────────────────────────────────────────────────────────────────
void DB::maybe_compact() {
    if ((int)sst_readers_.size() < compaction_trigger_) return;

    // Collect paths (oldest first for compaction merge ordering)
    std::vector<std::string> paths;
    paths.reserve(sst_readers_.size());
    for (auto it = sst_readers_.rbegin(); it != sst_readers_.rend(); ++it) {
        paths.push_back((*it)->path());
    }

    // Close all readers before file ops
    sst_readers_.clear();

    std::string out_path = sstable::make_sst_path(data_dir_, next_sst_seq_);
    bool ok = compaction::Compactor::compact(paths, out_path, index_interval_);

    // Delete old SST files
    for (auto& p : paths) {
        try { fs::remove(p); } catch (...) {}
    }

    if (ok) {
        ++next_sst_seq_;
        // Invalidate entire cache (keys may have changed)
        block_cache_->clear();
    }

    // Reload
    reload_sst_readers();
}

// ─────────────────────────────────────────────────────────────────────────────
// lsm_write: WAL → Memtable.  Caller must hold mu_.
// ─────────────────────────────────────────────────────────────────────────────
void DB::lsm_write(const std::string& key, const std::string& value,
                   int64_t expiry_ms, bool tombstone)
{
    // WAL (outside lock since WAL has its own mutex; but we hold mu_ here)
    if (wal_) {
        std::string wal_entry = tombstone
            ? wal_encode_del(key)
            : wal_encode_set(key, value, expiry_ms);
        wal_->append(wal_entry);
    }

    // Memtable
    if (tombstone) {
        memtable_->put_tombstone(key);
        block_cache_->invalidate(key);
    } else {
        memtable_->put(key, value, expiry_ms);
        block_cache_->invalidate(key);
    }

    // Flush if over threshold
    if (memtable_->size_bytes() >= memtable_max_bytes_) {
        flush_memtable();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// get_from_sst: search SSTable chain (newest first) with bloom + cache
// ─────────────────────────────────────────────────────────────────────────────
std::optional<Entry> DB::get_from_sst(const std::string& key) {
    // Check LRU cache first
    {
        auto cached = block_cache_->get(key);
        if (cached.has_value()) {
            ++cache_hits_;
            // Decode: value is stored as "expiry_ms\n<value>" internally
            // We store a simple JSON-like encoding: see cache put below
            // Actually we store raw value (no expiry in cache — expired checked at memtable level)
            // If cache has it, it was live when put
            // Return with expiry 0 (cache entries are already filtered)
            Entry e;
            e.value = *cached;
            e.expiry_ms = 0;
            e.tombstone = false;
            return e;
        }
        ++cache_misses_;
    }

    // Walk SSTable readers newest-first
    for (auto& reader : sst_readers_) {
        auto rec = reader->get(key);
        if (!rec.has_value()) {
            // Bloom filter said no (or not found after scan)
            // We can't distinguish bloom-skip from "scanned but missing"
            // The SSTableReader returns nullopt for both
            continue;
        }

        // Found in SSTable
        if (rec->tombstone) {
            return std::nullopt;  // deleted
        }

        int64_t now = now_ms();
        if (rec->expiry_ms > 0 && now >= rec->expiry_ms) {
            return std::nullopt;  // expired
        }

        Entry e{rec->value, rec->expiry_ms, false};
        // Cache the value
        block_cache_->put(key, rec->value);
        return e;
    }

    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Core DB operations
// ─────────────────────────────────────────────────────────────────────────────
void DB::set(const std::string& key, const std::string& value, int64_t expiry_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    lsm_write(key, value, expiry_ms, false);
}

std::optional<std::string> DB::get(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();

    // 1. Check memtable
    auto me = memtable_->get(key);
    if (me.has_value()) {
        if (me->tombstone) return std::nullopt;
        if (me->is_expired(now)) return std::nullopt;
        return me->value;
    }

    // 2. Check SSTables
    auto se = get_from_sst(key);
    if (!se.has_value()) return std::nullopt;
    return se->value;
}

bool DB::del(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    // Check if key actually exists before writing tombstone
    int64_t now = now_ms();
    bool exists = false;

    auto me = memtable_->get(key);
    if (me.has_value() && !me->tombstone && !me->is_expired(now)) {
        exists = true;
    } else if (!me.has_value()) {
        auto se = get_from_sst(key);
        if (se.has_value()) exists = true;
    }

    lsm_write(key, "", 0, true);  // write tombstone regardless (idempotent)
    return exists;
}

bool DB::exists(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();

    auto me = memtable_->get(key);
    if (me.has_value()) {
        return !me->tombstone && !me->is_expired(now);
    }

    auto se = get_from_sst(key);
    return se.has_value();
}

bool DB::expire(const std::string& key, int64_t expiry_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();

    // Look up current value first
    std::string val;
    int64_t old_expiry = 0;

    auto me = memtable_->get(key);
    if (me.has_value()) {
        if (me->tombstone || me->is_expired(now)) return false;
        val = me->value;
        old_expiry = me->expiry_ms;
    } else {
        auto se = get_from_sst(key);
        if (!se.has_value()) return false;
        val = se->value;
        old_expiry = se->expiry_ms;
    }
    (void)old_expiry;

    lsm_write(key, val, expiry_ms, false);
    return true;
}

int64_t DB::ttl_ms(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();

    auto lookup = [&]() -> std::optional<Entry> {
        auto me = memtable_->get(key);
        if (me.has_value()) {
            if (me->tombstone) return std::nullopt;
            if (me->is_expired(now)) return std::nullopt;
            return Entry{me->value, me->expiry_ms, false};
        }
        return get_from_sst(key);
    };

    auto e = lookup();
    if (!e.has_value()) return -2;
    if (e->expiry_ms == 0) return -1;
    return e->expiry_ms - now;
}

std::vector<std::string> DB::keys(const std::string& pattern) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();

    // Collect from memtable
    std::unordered_map<std::string, bool> seen;  // key → alive
    std::vector<std::string> result;

    for (auto it = memtable_->begin(); it != memtable_->end(); ++it) {
        seen[it->first] = true;
        if (!it->second.tombstone && !it->second.is_expired(now)) {
            if (glob_match(pattern, it->first)) {
                result.push_back(it->first);
            }
        }
    }

    // Collect from SSTables (skip keys already in memtable)
    // Since SSTs may be large, we do a full-scan via reading with compaction's StreamReader approach.
    // For now we rely on KEYS being O(N) per spec — we do best-effort by
    // iterating the sparse index as a guide, but actual KEYS needs full scan.
    // Practical approach: re-use SSTableReader::get is O(log N) per key,
    // but we don't have a full-scan iterator on SSTableReader (only get()).
    // We implement a simple approach: since the README says KEYS does a full
    // scan (same as Redis in single-instance mode), we open a streaming reader
    // inline here.

    // We need to collect all unique keys from all SSTables.
    // Use filesystem scan with StreamReader (defined in compaction.cpp internally).
    // To avoid code duplication, we'll implement an inline scan using raw file I/O,
    // replicating what compaction's StreamReader does.
    struct SSTScanner {
        std::ifstream in;
        uint64_t      data_end = 0;

        static uint32_t read_u32(std::ifstream& f) {
            char b[4]; f.read(b, 4);
            return uint8_t(b[0])|(uint8_t(b[1])<<8)|(uint8_t(b[2])<<16)|(uint8_t(b[3])<<24);
        }
        static uint64_t read_u64(std::ifstream& f) {
            char b[8]; f.read(b, 8);
            uint64_t v=0; for(int i=0;i<8;++i) v|=(uint64_t(uint8_t(b[i]))<<(8*i)); return v;
        }
        static std::string read_str(std::ifstream& f) {
            uint32_t len = read_u32(f);
            if (!f || len > 64*1024*1024) return {};
            std::string s(len, '\0'); f.read(s.data(), len); return s;
        }

        bool open(const std::string& path) {
            in.open(path, std::ios::binary);
            if (!in) return false;
            in.seekg(-28, std::ios::end);
            data_end = read_u64(in);
            in.seekg(0);
            return true;
        }

        struct Rec { std::string key, value; int64_t expiry_ms; bool tombstone; };

        bool next(Rec& out) {
            if (!in) return false;
            if ((uint64_t)in.tellg() >= data_end) return false;
            out.key = read_str(in);
            if (!in || out.key.empty()) return false;
            out.value     = read_str(in);
            out.expiry_ms = (int64_t)read_u64(in);
            char ts; in.get(ts);
            out.tombstone = (ts != 0);
            return (bool)in;
        }
    };

    // Scan all SSTables oldest-first so newer entries override older
    // (we want final state: scan newest-first, skip already-seen keys)
    // Actually scan newest-first: skip if key already seen in memtable or newer SST
    for (auto& reader : sst_readers_) {
        SSTScanner scanner;
        if (!scanner.open(reader->path())) continue;
        SSTScanner::Rec rec;
        while (scanner.next(rec)) {
            if (seen.count(rec.key)) continue;  // shadowed by memtable or newer SST
            seen[rec.key] = true;
            if (!rec.tombstone && (rec.expiry_ms == 0 || now < rec.expiry_ms)) {
                if (glob_match(pattern, rec.key)) {
                    result.push_back(rec.key);
                }
            }
        }
    }

    return result;
}

size_t DB::dbsize() {
    // For efficiency, use keys("*") count
    auto ks = keys("*");
    return ks.size();
}

void DB::flush_all() {
    std::lock_guard<std::mutex> lk(mu_);

    // Clear memtable
    memtable_->clear();
    if (wal_) wal_->truncate();

    // Delete all SST files
    sst_readers_.clear();
    try {
        for (auto& entry : fs::directory_iterator(data_dir_)) {
            if (entry.path().extension() == ".sst") {
                fs::remove(entry.path());
            }
        }
    } catch (...) {}

    next_sst_seq_ = 0;
    block_cache_->clear();
}

int64_t DB::incr(const std::string& key, int64_t delta) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();

    // Get current value
    std::string cur_val;
    int64_t     cur_expiry = 0;
    bool        found = false;

    auto me = memtable_->get(key);
    if (me.has_value() && !me->tombstone && !me->is_expired(now)) {
        cur_val = me->value;
        cur_expiry = me->expiry_ms;
        found = true;
    } else if (!me.has_value()) {
        auto se = get_from_sst(key);
        if (se.has_value()) {
            cur_val = se->value;
            cur_expiry = se->expiry_ms;
            found = true;
        }
    }

    int64_t num = 0;
    if (found) {
        try { num = std::stoll(cur_val); } catch (...) {
            throw std::runtime_error("ERR value is not an integer or out of range");
        }
    }

    num += delta;
    std::string new_val = std::to_string(num);
    lsm_write(key, new_val, cur_expiry, false);
    return num;
}

size_t DB::append(const std::string& key, const std::string& suffix) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();

    std::string cur_val;
    int64_t     cur_expiry = 0;

    auto me = memtable_->get(key);
    if (me.has_value() && !me->tombstone && !me->is_expired(now)) {
        cur_val = me->value;
        cur_expiry = me->expiry_ms;
    } else if (!me.has_value()) {
        auto se = get_from_sst(key);
        if (se.has_value()) {
            cur_val = se->value;
            cur_expiry = se->expiry_ms;
        }
    }

    cur_val += suffix;
    lsm_write(key, cur_val, cur_expiry, false);
    return cur_val.size();
}

size_t DB::strlen(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = now_ms();

    // Read from memtable first
    auto me = memtable_->get(key);
    if (me.has_value()) {
        if (me->tombstone || me->is_expired(now)) return 0;
        return me->value.size();
    }
    // Then SSTables
    auto se = get_from_sst(key);
    return se.has_value() ? se->value.size() : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Metric accessors
// ─────────────────────────────────────────────────────────────────────────────
size_t DB::memtable_size_bytes() const {
    std::lock_guard<std::mutex> lk(mu_);
    return memtable_->size_bytes();
}

int DB::sstable_count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return static_cast<int>(sst_readers_.size());
}

uint64_t DB::bloom_skips()   const { return bloom_skips_.load(); }
uint64_t DB::cache_hits()    const { return cache_hits_.load(); }
uint64_t DB::cache_misses()  const { return cache_misses_.load(); }

} // namespace minired::storage
