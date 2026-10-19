#include "commands/command_registry.h"
#include "protocol/resp_parser.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace minired::commands {

using namespace minired::protocol;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::string err(const std::string& msg) {
    return serialize_error(msg);
}

static std::string ok() {
    return serialize_simple("OK");
}

static int64_t now_ms_cmd() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// PING
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_ping(const Args& args, storage::DB&) {
    if (args.size() == 1) return serialize_simple("PONG");
    if (args.size() == 2) return serialize_bulk(args[1]);
    return err("ERR wrong number of arguments for 'ping' command");
}

// ─────────────────────────────────────────────────────────────────────────────
// ECHO
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_echo(const Args& args, storage::DB&) {
    if (args.size() != 2)
        return err("ERR wrong number of arguments for 'echo' command");
    return serialize_bulk(args[1]);
}

// ─────────────────────────────────────────────────────────────────────────────
// SET key value [EX seconds | PX milliseconds]
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_set(const Args& args, storage::DB& db) {
    if (args.size() < 3)
        return err("ERR wrong number of arguments for 'set' command");

    int64_t expiry_ms = 0;
    for (size_t i = 3; i < args.size(); i += 2) {
        std::string opt = to_upper(args[i]);
        if (i + 1 >= args.size())
            return err("ERR syntax error");
        int64_t val;
        try { val = std::stoll(args[i+1]); } catch (...) {
            return err("ERR value is not an integer or out of range");
        }
        if (opt == "EX") {
            if (val <= 0) return err("ERR invalid expire time in 'set' command");
            expiry_ms = now_ms_cmd() + val * 1000;
        } else if (opt == "PX") {
            if (val <= 0) return err("ERR invalid expire time in 'set' command");
            expiry_ms = now_ms_cmd() + val;
        } else {
            return err("ERR syntax error");
        }
    }
    db.set(args[1], args[2], expiry_ms);
    return ok();
}

// ─────────────────────────────────────────────────────────────────────────────
// GET key
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_get(const Args& args, storage::DB& db) {
    if (args.size() != 2)
        return err("ERR wrong number of arguments for 'get' command");
    auto val = db.get(args[1]);
    if (!val.has_value()) return serialize_null_bulk();
    return serialize_bulk(*val);
}

// ─────────────────────────────────────────────────────────────────────────────
// DEL key [key ...]
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_del(const Args& args, storage::DB& db) {
    if (args.size() < 2)
        return err("ERR wrong number of arguments for 'del' command");
    int64_t count = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        if (db.del(args[i])) ++count;
    }
    return serialize_integer(count);
}

// ─────────────────────────────────────────────────────────────────────────────
// EXISTS key [key ...]
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_exists(const Args& args, storage::DB& db) {
    if (args.size() < 2)
        return err("ERR wrong number of arguments for 'exists' command");
    int64_t count = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        if (db.exists(args[i])) ++count;
    }
    return serialize_integer(count);
}

// ─────────────────────────────────────────────────────────────────────────────
// INCR / DECR
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_incr(const Args& args, storage::DB& db) {
    if (args.size() != 2)
        return err("ERR wrong number of arguments for 'incr' command");
    try {
        return serialize_integer(db.incr(args[1], 1));
    } catch (const std::exception& e) {
        return err(e.what());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// INCRBY key delta / DECRBY key delta
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_incrby(const Args& args, storage::DB& db) {
    if (args.size() != 3)
        return err("ERR wrong number of arguments for 'incrby' command");
    int64_t delta;
    try { delta = std::stoll(args[2]); } catch (...) {
        return err("ERR value is not an integer or out of range");
    }
    try {
        return serialize_integer(db.incr(args[1], delta));
    } catch (const std::exception& e) {
        return err(e.what());
    }
}

static std::string cmd_decrby(const Args& args, storage::DB& db) {
    if (args.size() != 3)
        return err("ERR wrong number of arguments for 'decrby' command");
    int64_t delta;
    try { delta = std::stoll(args[2]); } catch (...) {
        return err("ERR value is not an integer or out of range");
    }
    try {
        return serialize_integer(db.incr(args[1], -delta));
    } catch (const std::exception& e) {
        return err(e.what());
    }
}

static std::string cmd_decr(const Args& args, storage::DB& db) {
    if (args.size() != 2)
        return err("ERR wrong number of arguments for 'decr' command");
    try {
        return serialize_integer(db.incr(args[1], -1));
    } catch (const std::exception& e) {
        return err(e.what());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// EXPIRE key seconds
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_expire(const Args& args, storage::DB& db) {
    if (args.size() != 3)
        return err("ERR wrong number of arguments for 'expire' command");
    int64_t secs;
    try { secs = std::stoll(args[2]); } catch (...) {
        return err("ERR value is not an integer or out of range");
    }
    if (secs <= 0) return err("ERR invalid expire time in 'expire' command");
    bool ok_ = db.expire(args[1], now_ms_cmd() + secs * 1000);
    return serialize_integer(ok_ ? 1 : 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// TTL key   → seconds remaining; -1 = persistent; -2 = not found
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_ttl(const Args& args, storage::DB& db) {
    if (args.size() != 2)
        return err("ERR wrong number of arguments for 'ttl' command");
    int64_t ms = db.ttl_ms(args[1]);
    if (ms == -1) return serialize_integer(-1);
    if (ms == -2) return serialize_integer(-2);
    return serialize_integer(ms / 1000);  // convert ms → seconds (Redis TTL is in seconds)
}

// ─────────────────────────────────────────────────────────────────────────────
// KEYS pattern
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_keys(const Args& args, storage::DB& db) {
    if (args.size() != 2)
        return err("ERR wrong number of arguments for 'keys' command");
    auto ks = db.keys(args[1]);
    std::string out = "*" + std::to_string(ks.size()) + "\r\n";
    for (auto& k : ks) out += serialize_bulk(k);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// DBSIZE
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_dbsize(const Args& args, storage::DB& db) {
    if (args.size() != 1)
        return err("ERR wrong number of arguments for 'dbsize' command");
    return serialize_integer(static_cast<int64_t>(db.dbsize()));
}

// ─────────────────────────────────────────────────────────────────────────────
// FLUSHALL
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// APPEND key value
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_append(const Args& args, storage::DB& db) {
    if (args.size() != 3)
        return err("ERR wrong number of arguments for 'append' command");
    size_t new_len = db.append(args[1], args[2]);
    return serialize_integer(static_cast<int64_t>(new_len));
}

// ─────────────────────────────────────────────────────────────────────────────
// STRLEN key
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_strlen(const Args& args, storage::DB& db) {
    if (args.size() != 2)
        return err("ERR wrong number of arguments for 'strlen' command");
    return serialize_integer(static_cast<int64_t>(db.strlen(args[1])));
}

// ─────────────────────────────────────────────────────────────────────────────
// GETSET key value  (atomic get-then-set, deprecated in Redis 6.2 but common)
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_getset(const Args& args, storage::DB& db) {
    if (args.size() != 3)
        return err("ERR wrong number of arguments for 'getset' command");
    auto old_val = db.get(args[1]);
    db.set(args[1], args[2]);
    if (!old_val.has_value()) return serialize_null_bulk();
    return serialize_bulk(*old_val);
}

// ─────────────────────────────────────────────────────────────────────────────
// MSET key value [key value ...]
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_mset(const Args& args, storage::DB& db) {
    if (args.size() < 3 || (args.size() % 2) == 0)
        return err("ERR wrong number of arguments for 'mset' command");
    for (size_t i = 1; i < args.size(); i += 2) {
        db.set(args[i], args[i + 1]);
    }
    return ok();
}

// ─────────────────────────────────────────────────────────────────────────────
// MGET key [key ...]
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_mget(const Args& args, storage::DB& db) {
    if (args.size() < 2)
        return err("ERR wrong number of arguments for 'mget' command");
    std::string out = "*" + std::to_string(args.size() - 1) + "\r\n";
    for (size_t i = 1; i < args.size(); ++i) {
        auto val = db.get(args[i]);
        if (val.has_value()) out += serialize_bulk(*val);
        else                 out += serialize_null_bulk();
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// PEXPIRE key milliseconds
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_pexpire(const Args& args, storage::DB& db) {
    if (args.size() != 3)
        return err("ERR wrong number of arguments for 'pexpire' command");
    int64_t ms;
    try { ms = std::stoll(args[2]); } catch (...) {
        return err("ERR value is not an integer or out of range");
    }
    if (ms <= 0) return err("ERR invalid expire time in 'pexpire' command");
    bool ok_ = db.expire(args[1], now_ms_cmd() + ms);
    return serialize_integer(ok_ ? 1 : 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// PTTL key   → milliseconds remaining; -1 = persistent; -2 = not found
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_pttl(const Args& args, storage::DB& db) {
    if (args.size() != 2)
        return err("ERR wrong number of arguments for 'pttl' command");
    int64_t ms = db.ttl_ms(args[1]);
    return serialize_integer(ms);  // already in milliseconds
}

// ─────────────────────────────────────────────────────────────────────────────
// SETNX key value  (set if not exists)
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_setnx(const Args& args, storage::DB& db) {
    if (args.size() != 3)
        return err("ERR wrong number of arguments for 'setnx' command");
    if (db.exists(args[1])) return serialize_integer(0);
    db.set(args[1], args[2]);
    return serialize_integer(1);
}

static std::string cmd_flushall(const Args& args, storage::DB& db) {
    (void)args;
    db.flush_all();
    return ok();
}

// ─────────────────────────────────────────────────────────────────────────────
// INFO [section]
// ─────────────────────────────────────────────────────────────────────────────
static std::string cmd_info(const Args& args, storage::DB& db) {
    (void)args;
    std::ostringstream ss;
    ss << "# Server\r\n"
       << "redis_version:0.1.0-minired\r\n"
       << "redis_mode:standalone\r\n"
       << "os:Linux\r\n"
       << "arch_bits:64\r\n"
       << "multiplexing_api:epoll\r\n"
       << "\r\n"
       << "# Stats\r\n"
       << "total_commands_processed:" << db.total_commands_processed() << "\r\n"
       << "\r\n"
       << "# Keyspace\r\n"
       << "db0:keys=" << db.dbsize() << "\r\n"
       << "\r\n"
       << "# MiniRed\r\n"
       << "memtable_size_bytes:" << db.memtable_size_bytes() << "\r\n"
       << "sstable_count:"       << db.sstable_count()       << "\r\n"
       << "bloom_filter_skips:"  << db.bloom_skips()         << "\r\n"
       << "cache_hits:"          << db.cache_hits()           << "\r\n"
       << "cache_misses:"        << db.cache_misses()         << "\r\n";
    return serialize_bulk(ss.str());
}

// ─────────────────────────────────────────────────────────────────────────────
// CommandRegistry
// ─────────────────────────────────────────────────────────────────────────────
void CommandRegistry::reg(const std::string& name, Handler h) {
    handlers_[name] = std::move(h);
}

void CommandRegistry::register_all() {
    reg("PING",     cmd_ping);
    reg("ECHO",     cmd_echo);
    reg("SET",      cmd_set);
    reg("GET",      cmd_get);
    reg("DEL",      cmd_del);
    reg("EXISTS",   cmd_exists);
    reg("INCR",     cmd_incr);
    reg("DECR",     cmd_decr);
    reg("INCRBY",   cmd_incrby);
    reg("DECRBY",   cmd_decrby);
    reg("APPEND",   cmd_append);
    reg("STRLEN",   cmd_strlen);
    reg("GETSET",   cmd_getset);
    reg("MSET",     cmd_mset);
    reg("MGET",     cmd_mget);
    reg("EXPIRE",   cmd_expire);
    reg("PEXPIRE",  cmd_pexpire);
    reg("TTL",      cmd_ttl);
    reg("PTTL",     cmd_pttl);
    reg("SETNX",    cmd_setnx);
    reg("KEYS",     cmd_keys);
    reg("DBSIZE",   cmd_dbsize);
    reg("FLUSHALL", cmd_flushall);
    reg("INFO",     cmd_info);
}

std::string CommandRegistry::dispatch(const Args& args, storage::DB& db) const {
    if (args.empty()) return err("ERR empty command");
    std::string name = to_upper(args[0]);
    auto it = handlers_.find(name);
    if (it == handlers_.end()) {
        return err("ERR unknown command '" + args[0] +
                   "', with args beginning with: " +
                   (args.size() > 1 ? "'" + args[1] + "'" : ""));
    }
    db.count_command();
    return it->second(args, db);
}

} // namespace minired::commands
