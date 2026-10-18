#include "storage/compaction/compaction.h"
#include <queue>
#include <chrono>
#include <iostream>
#include <fstream>

namespace minired::storage::compaction {

using namespace sstable;

static int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Per-reader iterator state for k-way merge heap
struct HeapEntry {
    std::string key;
    std::string value;
    int64_t     expiry_ms;
    bool        tombstone;
    int         reader_idx;  // which reader this came from

    bool operator>(const HeapEntry& o) const {
        // Min-heap on key; tie-break: lower reader_idx = newer = wins
        if (key != o.key) return key > o.key;
        return reader_idx > o.reader_idx;  // smaller idx wins (newer file)
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Streaming reader that iterates all records in an SSTable in order.
// We open the raw binary file and skip the header/index/bloom by reading
// only from offset 0 up to footer_.index_offset.
// ─────────────────────────────────────────────────────────────────────────────
struct StreamReader {
    std::ifstream in;
    uint64_t      data_end = 0;
    bool          valid    = false;

    static uint32_t read_u32(std::ifstream& f) {
        char b[4]; f.read(b,4);
        return uint8_t(b[0])|(uint8_t(b[1])<<8)|(uint8_t(b[2])<<16)|(uint8_t(b[3])<<24);
    }
    static uint64_t read_u64(std::ifstream& f) {
        char b[8]; f.read(b,8);
        uint64_t v=0; for(int i=0;i<8;++i) v|=(uint64_t(uint8_t(b[i]))<<(8*i)); return v;
    }
    static std::string read_str(std::ifstream& f) {
        uint32_t len=read_u32(f); std::string s(len,'\0'); f.read(s.data(),len); return s;
    }

    bool open(const std::string& path) {
        in.open(path, std::ios::binary);
        if (!in) return false;
        // Read footer to find data_end = index_offset
        in.seekg(-28, std::ios::end);
        data_end = read_u64(in);  // index_offset
        uint32_t idx_count = read_u32(in);
        (void)idx_count;
        in.seekg(0);
        valid = true;
        return true;
    }

    bool next(Record& out) {
        if (!valid || !in) return false;
        if (static_cast<uint64_t>(in.tellg()) >= data_end) return false;
        out.key       = read_str(in);
        if (!in || out.key.empty()) return false;
        out.value     = read_str(in);
        out.expiry_ms = static_cast<int64_t>(read_u64(in));
        char ts; in.get(ts);
        out.tombstone = (ts != 0);
        return static_cast<bool>(in);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
bool Compactor::compact(const std::vector<std::string>& sst_paths,
                        const std::string& out_path,
                        int index_interval)
{
    int n = static_cast<int>(sst_paths.size());
    std::vector<StreamReader> readers(n);
    for (int i = 0; i < n; ++i) {
        if (!readers[i].open(sst_paths[i])) {
            std::cerr << "Compaction: cannot open " << sst_paths[i] << "\n";
            return false;
        }
    }

    // Initialize min-heap with first record from each reader
    using MinHeap = std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>>;
    MinHeap heap;

    for (int i = 0; i < n; ++i) {
        Record rec;
        if (readers[i].next(rec)) {
            heap.push({rec.key, rec.value, rec.expiry_ms, rec.tombstone, i});
        }
    }

    SSTableWriter writer(out_path, index_interval);
    int64_t now = now_ms();
    std::string last_written_key;

    while (!heap.empty()) {
        HeapEntry top = heap.top(); heap.pop();

        // Advance that reader
        {
            Record nxt;
            if (readers[top.reader_idx].next(nxt)) {
                heap.push({nxt.key, nxt.value, nxt.expiry_ms, nxt.tombstone, top.reader_idx});
            }
        }

        // Skip duplicates: we already emitted this key from a newer source
        if (top.key == last_written_key) continue;

        // Skip expired keys (non-tombstones with past expiry)
        if (!top.tombstone && top.expiry_ms > 0 && now >= top.expiry_ms) continue;

        // Skip tombstones: in a full compaction there are no older files, so
        // tombstones can be dropped. (If this is a partial compaction, keep them.)
        // Here we always drop tombstones since Compactor merges ALL files.
        if (top.tombstone) {
            last_written_key = top.key;
            continue;
        }

        writer.add({top.key, top.value, top.expiry_ms, false});
        last_written_key = top.key;
    }

    writer.finish();
    return true;
}

} // namespace minired::storage::compaction
