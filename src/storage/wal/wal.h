#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace minired::storage::wal {

// ── WAL entry format ─────────────────────────────────────────────────────────
// [4B entry_len (little-endian)] [entry_len bytes: raw RESP command]
//
// A full RESP-encoded command (e.g. *3\r\n$3\r\nSET\r\n...) is appended
// so that on recovery we can replay it through the normal command dispatcher.

class WAL {
public:
    // fsync_policy: "always" | "everysec" | "no"
    explicit WAL(const std::string& path, const std::string& fsync_policy);
    ~WAL();

    // Open (or create) the WAL file.
    void open();

    // Append one RESP-encoded command; returns after durability guarantee.
    void append(const std::string& resp_cmd);

    // Truncate the WAL (called after a successful memtable flush to SSTable).
    void truncate();

    // Replay all entries. Calls `cb` for each recovered RESP command string.
    using ReplayCb = std::function<void(const std::string& resp_cmd)>;
    void replay(const ReplayCb& cb);

    void close();

private:
    std::string       path_;
    std::string       fsync_policy_;
    std::ofstream     out_;
    int               fd_    = -1;   // raw fd for fsync
    std::mutex        mu_;

    // "everysec" background thread
    std::thread       sync_thread_;
    std::atomic<bool> stop_sync_{false};
    std::atomic<bool> needs_sync_{false};
    void sync_loop();

    void do_fsync();
    static void write_u32le(std::ofstream& f, uint32_t v);
    static uint32_t read_u32le(std::ifstream& f);
};

} // namespace minired::storage::wal
