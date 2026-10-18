#include "server/config.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace minired {

Config Config::parse(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + arg);
            return argv[++i];
        };
        if (arg == "--port")              cfg.port = static_cast<uint16_t>(std::stoi(next()));
        else if (arg == "--bind")         cfg.bind_addr = next();
        else if (arg == "--data-dir")     cfg.data_dir  = next();
        else if (arg == "--fsync")        cfg.fsync_policy = next();
        else if (arg == "--memtable-mb")  cfg.memtable_max_bytes = std::stoull(next()) * 1024 * 1024;
        else if (arg == "--cache-mb")     cfg.block_cache_bytes  = std::stoull(next()) * 1024 * 1024;
        else if (arg == "--compact-at")   cfg.compaction_trigger = std::stoi(next());
        else {
            std::cerr << "Unknown flag: " << arg << "\n";
        }
    }
    return cfg;
}

} // namespace minired
