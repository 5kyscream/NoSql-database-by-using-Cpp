#include "server/config.h"
#include "server/server.h"
#include "storage/db.h"
#include <iostream>
#include <filesystem>

int main(int argc, char** argv) {
    auto cfg = minired::Config::parse(argc, argv);

    // Ensure data directory exists
    std::filesystem::create_directories(cfg.data_dir);

    minired::storage::DB db(cfg.data_dir, cfg.fsync_policy, cfg.memtable_max_bytes);
    db.open();

    try {
        minired::Server server(cfg, db);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        db.close();
        return 1;
    }

    db.close();
    std::cout << "MiniRed shutdown cleanly.\n";
    return 0;
}
