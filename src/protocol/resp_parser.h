#pragma once
#include <string>
#include <vector>
#include <variant>
#include <cstdint>

namespace minired::protocol {

// ── RESP2 value types ────────────────────────────────────────────────────────
struct SimpleString { std::string value; };
struct ErrorString  { std::string value; };
struct Integer      { int64_t     value; };
struct BulkString   { std::string value; bool is_null = false; };
struct NullBulk     {};
struct Array;

using RespValue = std::variant<
    SimpleString,
    ErrorString,
    Integer,
    BulkString,
    NullBulk,
    std::vector<RespValue>   // Array (recursive via vector)
>;

// ── Parser ───────────────────────────────────────────────────────────────────
// Feed raw bytes incrementally; returns true when a complete command is parsed.
// Caller drains parsed_commands after each call.
class RespParser {
public:
    // Feed bytes into the parser. Returns true if at least one command is ready.
    bool feed(const char* data, size_t len);

    // Whether at least one complete command is waiting.
    bool has_command() const { return !commands_.empty(); }

    // Pop the next fully-parsed command (an Array of BulkStrings).
    std::vector<std::string> pop_command();

    void reset();

private:
    std::string buf_;           // raw bytes not yet consumed

    // Parsed commands waiting to be dispatched.
    std::vector<std::vector<std::string>> commands_;

    // --- parse helpers ---
    // Returns number of bytes consumed, or -1 if incomplete.
    int parse_one();
    int parse_inline(size_t start);
    int parse_array(size_t start, std::vector<std::string>& out);
    // Returns bulk-string value and bytes consumed; -1 = incomplete.
    int parse_bulk(size_t start, std::string& out);
};

// ── Serializer ───────────────────────────────────────────────────────────────
std::string serialize_simple(const std::string& s);
std::string serialize_error(const std::string& s);
std::string serialize_integer(int64_t n);
std::string serialize_bulk(const std::string& s);
std::string serialize_null_bulk();
std::string serialize_array(const std::vector<std::string>& items);
std::string serialize_null_array();

} // namespace minired::protocol
