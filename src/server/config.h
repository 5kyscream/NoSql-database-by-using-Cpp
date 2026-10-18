#pragma once
#include <string>
#include <cstdint>

namespace minired {

// ── Server configuration (can be overridden by command-line flags) ───────────
struct Config {
    // Network
    std::string  bind_addr   = "0.0.0.0";
    uint16_t     port        = 6379;
    int          backlog     = 128;

    // Storage
    std::string  data_dir    = "data";
    size_t       memtable_max_bytes = 64 * 1024 * 1024; // 64 MiB
    int          sstable_index_interval = 16;           // every 16th key in sparse index

    // WAL fsync policy: "always" | "everysec" | "no"
    std::string  fsync_policy = "everysec";

    // Compaction
    int          compaction_trigger = 4;   // compact when >= N SSTables

    // Cache
    size_t       block_cache_bytes = 8 * 1024 * 1024;  // 8 MiB

    static Config parse(int argc, char** argv);
};

} // namespace minired
