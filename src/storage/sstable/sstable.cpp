#include "storage/sstable/sstable.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace minired::storage::sstable {

// ─────────────────────────────────────────────────────────────────────────────
// SSTableWriter wire helpers
// ─────────────────────────────────────────────────────────────────────────────
void SSTableWriter::write_u32(std::ofstream& f, uint32_t v) {
    char b[4] = {char(v),char(v>>8),char(v>>16),char(v>>24)};
    f.write(b, 4);
}
void SSTableWriter::write_u64(std::ofstream& f, uint64_t v) {
    char b[8];
    for (int i=0;i<8;++i) b[i]=char(v>>(8*i));
    f.write(b, 8);
}
void SSTableWriter::write_str(std::ofstream& f, const std::string& s) {
    write_u32(f, static_cast<uint32_t>(s.size()));
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// SSTableReader wire helpers
// ─────────────────────────────────────────────────────────────────────────────
uint32_t SSTableReader::read_u32(std::ifstream& f) {
    char b[4]; f.read(b,4);
    return uint8_t(b[0])|(uint8_t(b[1])<<8)|(uint8_t(b[2])<<16)|(uint8_t(b[3])<<24);
}
uint64_t SSTableReader::read_u64(std::ifstream& f) {
    char b[8]; f.read(b,8);
    uint64_t v=0;
    for(int i=0;i<8;++i) v|=(uint64_t(uint8_t(b[i]))<<(8*i));
    return v;
}
std::string SSTableReader::read_str(std::ifstream& f) {
    uint32_t len = read_u32(f);
    if (!f || len > 64*1024*1024) return {};
    std::string s(len, '\0');
    f.read(s.data(), len);
    return s;
}
Record SSTableReader::read_record(std::ifstream& f) {
    std::string k = read_str(f);
    if (!f) return {};
    std::string v = read_str(f);
    uint64_t exp = read_u64(f);
    char ts_byte = 0;
    f.get(ts_byte);
    return Record{k, v, static_cast<int64_t>(exp), ts_byte != 0};
}

// ─────────────────────────────────────────────────────────────────────────────
// SSTableWriter
// ─────────────────────────────────────────────────────────────────────────────
SSTableWriter::SSTableWriter(const std::string& path, int index_interval)
    : path_(path), index_interval_(index_interval)
{
    out_.open(path_, std::ios::binary | std::ios::trunc);
    if (!out_) throw std::runtime_error("SSTableWriter: cannot create " + path_);
}

SSTableWriter::~SSTableWriter() {
    if (out_.is_open()) out_.close();
}

uint64_t SSTableWriter::current_pos() {
    return static_cast<uint64_t>(out_.tellp());
}

void SSTableWriter::add(const Record& rec) {
    if (!bloom_.initialized()) {
        // We don't know total count upfront; use a generous pre-allocation.
        // For correctness, we always call init with a large m.
        // (Proper solution in production: two-pass write or in-memory buffering.)
        bloom_.init(1 << 20, 7);   // 1M bits ≈ 128 KB, 7 hashes → ~0.8% FPR at 100K keys
    }
    bloom_.add(rec.key);

    // Sparse index: record every Nth key's file offset
    if (record_count_ % index_interval_ == 0) {
        index_.push_back({rec.key, current_pos()});
    }

    // Record: [4B key_len][key][4B val_len][val][8B expiry_ms][1B tombstone]
    write_str(out_, rec.key);
    write_str(out_, rec.value);
    write_u64(out_, static_cast<uint64_t>(rec.expiry_ms));
    out_.put(rec.tombstone ? 1 : 0);

    ++record_count_;
}

void SSTableWriter::finish() {
    Footer footer;
    footer.index_offset = current_pos();
    footer.index_count  = static_cast<uint32_t>(index_.size());

    // Write sparse index
    for (auto& ie : index_) {
        write_str(out_, ie.key);
        write_u64(out_, ie.file_offset);
    }

    footer.bloom_offset = current_pos();
    auto bloom_bytes = bloom_.serialize();
    footer.bloom_size = static_cast<uint32_t>(bloom_bytes.size());
    out_.write(reinterpret_cast<const char*>(bloom_bytes.data()),
               static_cast<std::streamsize>(bloom_bytes.size()));

    // Footer: index_offset(8) + index_count(4) + bloom_offset(8) + bloom_size(4) + magic(4)
    write_u64(out_, footer.index_offset);
    write_u32(out_, footer.index_count);
    write_u64(out_, footer.bloom_offset);
    write_u32(out_, footer.bloom_size);
    write_u32(out_, Footer::kMagic);

    out_.flush();
    out_.close();
}

// ─────────────────────────────────────────────────────────────────────────────
// SSTableReader
// ─────────────────────────────────────────────────────────────────────────────
SSTableReader::SSTableReader(const std::string& path) : path_(path) {}

bool SSTableReader::open() {
    in_.open(path_, std::ios::binary);
    if (!in_) return false;
    if (!load_footer()) return false;
    if (!load_index())  return false;
    if (!load_bloom())  return false;
    valid_ = true;
    return true;
}

void SSTableReader::close() { in_.close(); valid_ = false; }

bool SSTableReader::load_footer() {
    // Footer is fixed 28 bytes from end of file.
    in_.seekg(0, std::ios::end);
    auto file_size = static_cast<uint64_t>(in_.tellg());
    if (file_size < 28) return false;

    in_.seekg(-28, std::ios::end);
    footer_.index_offset = read_u64(in_);
    footer_.index_count  = read_u32(in_);
    footer_.bloom_offset = read_u64(in_);
    footer_.bloom_size   = read_u32(in_);
    uint32_t magic       = read_u32(in_);
    return magic == Footer::kMagic;
}

bool SSTableReader::load_index() {
    in_.seekg(static_cast<std::streamoff>(footer_.index_offset));
    index_.resize(footer_.index_count);
    for (auto& ie : index_) {
        ie.key         = read_str(in_);
        ie.file_offset = read_u64(in_);
        if (!in_) return false;
    }
    return true;
}

bool SSTableReader::load_bloom() {
    in_.seekg(static_cast<std::streamoff>(footer_.bloom_offset));
    std::vector<uint8_t> blob(footer_.bloom_size);
    in_.read(reinterpret_cast<char*>(blob.data()),
             static_cast<std::streamsize>(footer_.bloom_size));
    if (!in_) return false;
    return bloom_.deserialize(blob);
}

std::optional<Record> SSTableReader::get(const std::string& key) {
    if (!valid_) return std::nullopt;

    // Bloom check — fast path
    if (!bloom_.might_contain(key)) return std::nullopt;

    // Binary search the sparse index for the largest index key <= target key
    if (index_.empty()) {
        in_.seekg(0);
    } else {
        // Find the last index entry whose key <= key
        int lo = 0, hi = static_cast<int>(index_.size()) - 1, best = 0;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (index_[mid].key <= key) { best = mid; lo = mid + 1; }
            else hi = mid - 1;
        }
        in_.seekg(static_cast<std::streamoff>(index_[best].file_offset));
    }

    // Linear scan from that position until we pass key or hit end of data section
    uint64_t data_end = index_.empty()
        ? footer_.index_offset
        : footer_.index_offset;

    while (static_cast<uint64_t>(in_.tellg()) < data_end && in_) {
        auto pos = static_cast<uint64_t>(in_.tellg());
        if (pos >= footer_.index_offset) break;

        std::string k = read_str(in_);
        if (!in_) break;
        std::string v = read_str(in_);
        uint64_t exp  = read_u64(in_);
        char ts_byte; in_.get(ts_byte);

        if (k == key) {
            return Record{k, v, static_cast<int64_t>(exp), ts_byte != 0};
        }
        if (k > key) break;  // Passed it — not in this file
    }
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
std::string make_sst_path(const std::string& data_dir, uint64_t seq) {
    char buf[32];
    snprintf(buf, sizeof(buf), "/%08llu.sst", (unsigned long long)seq);
    return data_dir + buf;
}

} // namespace minired::storage::sstable
