#include "storage/wal/wal.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <fstream>
#include <chrono>

namespace minired::storage::wal {

// ─────────────────────────────────────────────────────────────────────────────
WAL::WAL(const std::string& path, const std::string& fsync_policy)
    : path_(path), fsync_policy_(fsync_policy)
{}

WAL::~WAL() { close(); }

// ─────────────────────────────────────────────────────────────────────────────
void WAL::open() {
    // Open for reading first to replay, then open append-only write stream.
    out_.open(path_, std::ios::binary | std::ios::app);
    if (!out_) throw std::runtime_error("WAL: cannot open " + path_);

    // Raw fd for fsync
    fd_ = ::open(path_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd_ < 0) throw std::runtime_error("WAL: open() failed: " + std::string(strerror(errno)));

    if (fsync_policy_ == "everysec") {
        stop_sync_ = false;
        sync_thread_ = std::thread(&WAL::sync_loop, this);
    }
}

void WAL::close() {
    if (fsync_policy_ == "everysec" && sync_thread_.joinable()) {
        stop_sync_ = true;
        sync_thread_.join();
    }
    if (out_.is_open()) {
        do_fsync();
        out_.close();
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void WAL::write_u32le(std::ofstream& f, uint32_t v) {
    char buf[4];
    buf[0] = v & 0xFF;
    buf[1] = (v >> 8) & 0xFF;
    buf[2] = (v >> 16) & 0xFF;
    buf[3] = (v >> 24) & 0xFF;
    f.write(buf, 4);
}

uint32_t WAL::read_u32le(std::ifstream& f) {
    char buf[4];
    f.read(buf, 4);
    if (!f) return 0;
    return static_cast<uint8_t>(buf[0])
         | (static_cast<uint8_t>(buf[1]) << 8)
         | (static_cast<uint8_t>(buf[2]) << 16)
         | (static_cast<uint8_t>(buf[3]) << 24);
}

// ─────────────────────────────────────────────────────────────────────────────
void WAL::append(const std::string& resp_cmd) {
    std::lock_guard<std::mutex> lk(mu_);
    write_u32le(out_, static_cast<uint32_t>(resp_cmd.size()));
    out_.write(resp_cmd.data(), static_cast<std::streamsize>(resp_cmd.size()));
    out_.flush();

    if (fsync_policy_ == "always") {
        do_fsync();
    } else if (fsync_policy_ == "everysec") {
        needs_sync_ = true;
    }
    // "no": nothing
}

void WAL::do_fsync() {
    if (fd_ >= 0) ::fdatasync(fd_);
}

void WAL::sync_loop() {
    while (!stop_sync_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (needs_sync_.exchange(false)) {
            std::lock_guard<std::mutex> lk(mu_);
            do_fsync();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void WAL::replay(const ReplayCb& cb) {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return;  // No WAL yet

    while (in.peek() != EOF) {
        uint32_t len = read_u32le(in);
        if (!in || len == 0) break;
        if (len > 64 * 1024 * 1024) {
            std::cerr << "WAL: suspicious entry length " << len << " – stopping replay\n";
            break;
        }
        std::string cmd(len, '\0');
        in.read(cmd.data(), len);
        if (!in) {
            std::cerr << "WAL: truncated entry – stopping replay\n";
            break;
        }
        cb(cmd);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void WAL::truncate() {
    std::lock_guard<std::mutex> lk(mu_);
    out_.close();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }

    // Truncate the file to zero bytes.
    int tfd = ::open(path_.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (tfd >= 0) ::close(tfd);

    // Reopen
    out_.open(path_, std::ios::binary | std::ios::app);
    fd_ = ::open(path_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
}

} // namespace minired::storage::wal
