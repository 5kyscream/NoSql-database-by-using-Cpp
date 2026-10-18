#include "protocol/resp_parser.h"
#include <algorithm>
#include <stdexcept>
#include <cctype>

namespace minired::protocol {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static int find_crlf(const std::string& buf, size_t start) {
    for (size_t i = start; i + 1 < buf.size(); ++i) {
        if (buf[i] == '\r' && buf[i+1] == '\n') return static_cast<int>(i);
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// RespParser::feed
// ─────────────────────────────────────────────────────────────────────────────
bool RespParser::feed(const char* data, size_t len) {
    buf_.append(data, len);
    while (!buf_.empty()) {
        int consumed = parse_one();
        if (consumed <= 0) break;   // incomplete or error
        buf_.erase(0, static_cast<size_t>(consumed));
    }
    return !commands_.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_one: try to parse a single complete command from buf_[0...]
// Returns bytes consumed, or -1 if incomplete.
// ─────────────────────────────────────────────────────────────────────────────
int RespParser::parse_one() {
    if (buf_.empty()) return -1;

    if (buf_[0] == '*') {
        // Standard RESP array (multi-bulk)
        std::vector<std::string> cmd;
        int consumed = parse_array(0, cmd);
        if (consumed < 0) return -1;
        commands_.push_back(std::move(cmd));
        return consumed;
    } else {
        // Inline command (e.g. redis-cli in pipe mode, telnet tests)
        return parse_inline(0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_array: parse *N\r\n followed by N bulk strings
// ─────────────────────────────────────────────────────────────────────────────
int RespParser::parse_array(size_t start, std::vector<std::string>& out) {
    if (start >= buf_.size()) return -1;
    if (buf_[start] != '*') return -1;

    int crlf = find_crlf(buf_, start + 1);
    if (crlf < 0) return -1;

    int count = std::stoi(buf_.substr(start + 1, crlf - (start + 1)));
    if (count < 0) {
        // *-1\r\n  → null array (unusual from clients, handle gracefully)
        commands_.push_back({});
        return crlf + 2 - static_cast<int>(start);
    }

    size_t pos = static_cast<size_t>(crlf) + 2;
    out.clear();
    for (int i = 0; i < count; ++i) {
        std::string val;
        int bulk_consumed = parse_bulk(pos, val);
        if (bulk_consumed < 0) return -1;
        out.push_back(std::move(val));
        pos += static_cast<size_t>(bulk_consumed);
    }
    return static_cast<int>(pos) - static_cast<int>(start);
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_bulk: parse $N\r\n<data>\r\n
// ─────────────────────────────────────────────────────────────────────────────
int RespParser::parse_bulk(size_t start, std::string& out) {
    if (start >= buf_.size()) return -1;
    if (buf_[start] != '$') return -1;

    int crlf = find_crlf(buf_, start + 1);
    if (crlf < 0) return -1;

    int len = std::stoi(buf_.substr(start + 1, crlf - (start + 1)));
    if (len < 0) {
        out = "";       // null bulk
        return crlf + 2 - static_cast<int>(start);
    }

    size_t data_start = static_cast<size_t>(crlf) + 2;
    size_t data_end   = data_start + static_cast<size_t>(len);
    if (data_end + 2 > buf_.size()) return -1;   // incomplete

    out = buf_.substr(data_start, static_cast<size_t>(len));
    return static_cast<int>(data_end + 2) - static_cast<int>(start);
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_inline: split space-delimited line, handle quotes simply
// ─────────────────────────────────────────────────────────────────────────────
int RespParser::parse_inline(size_t start) {
    int crlf = find_crlf(buf_, start);
    if (crlf < 0) {
        // Also accept bare \n (some tools omit \r)
        size_t lf = buf_.find('\n', start);
        if (lf == std::string::npos) return -1;
        crlf = static_cast<int>(lf);
    }

    std::string line = buf_.substr(start, static_cast<size_t>(crlf) - start);
    // Strip trailing \r if present (for bare \n case)
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size()) {
        // skip spaces
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;

        if (line[i] == '"') {
            // quoted token
            ++i;
            std::string tok;
            while (i < line.size() && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < line.size()) { ++i; }
                tok += line[i++];
            }
            if (i < line.size()) ++i; // skip closing "
            tokens.push_back(tok);
        } else {
            size_t tok_start = i;
            while (i < line.size() && line[i] != ' ') ++i;
            tokens.push_back(line.substr(tok_start, i - tok_start));
        }
    }

    if (!tokens.empty()) commands_.push_back(std::move(tokens));

    // consumed = up to and including \r\n
    return crlf + 2 - static_cast<int>(start);
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> RespParser::pop_command() {
    auto cmd = std::move(commands_.front());
    commands_.erase(commands_.begin());
    return cmd;
}

void RespParser::reset() {
    buf_.clear();
    commands_.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Serializers
// ─────────────────────────────────────────────────────────────────────────────
std::string serialize_simple(const std::string& s) {
    return "+" + s + "\r\n";
}

std::string serialize_error(const std::string& s) {
    return "-" + s + "\r\n";
}

std::string serialize_integer(int64_t n) {
    return ":" + std::to_string(n) + "\r\n";
}

std::string serialize_bulk(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::string serialize_null_bulk() {
    return "$-1\r\n";
}

std::string serialize_null_array() {
    return "*-1\r\n";
}

std::string serialize_array(const std::vector<std::string>& items) {
    std::string out = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        out += serialize_bulk(item);
    }
    return out;
}

} // namespace minired::protocol
