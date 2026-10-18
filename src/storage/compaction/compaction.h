#pragma once
#include "storage/sstable/sstable.h"
#include <string>
#include <vector>
#include <memory>

namespace minired::storage::compaction {

// ── Compaction: k-way merge of SSTables ──────────────────────────────────────
// Merges a list of SSTables (newest first) into a single output SSTable,
// dropping overwritten values, pure tombstones (no older data), and
// expired keys.
class Compactor {
public:
    // Merge sst_paths (oldest last) into out_path.
    // After success, the input files can be deleted.
    // Returns true on success.
    static bool compact(const std::vector<std::string>& sst_paths,
                        const std::string& out_path,
                        int index_interval = 16);
};

} // namespace minired::storage::compaction
