#include "storage/memtable/memtable.h"
#include <stdexcept>

namespace minired::storage::memtable {

void Memtable::put(const std::string& key, const std::string& value, int64_t expiry_ms) {
    auto it = data_.find(key);
    if (it != data_.end()) {
        size_bytes_ -= it->first.size() + it->second.value.size() + 16;
        it->second = Entry{value, expiry_ms, false};
    } else {
        data_[key] = Entry{value, expiry_ms, false};
    }
    size_bytes_ += key.size() + value.size() + 16;
}

void Memtable::put_tombstone(const std::string& key) {
    auto it = data_.find(key);
    if (it != data_.end()) {
        size_bytes_ -= it->first.size() + it->second.value.size() + 16;
        it->second = Entry{"", 0, true};
    } else {
        data_[key] = Entry{"", 0, true};
    }
    size_bytes_ += key.size() + 16;
}

std::optional<Entry> Memtable::get(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    return it->second;
}

void Memtable::clear() {
    data_.clear();
    size_bytes_ = 0;
}

} // namespace minired::storage::memtable
