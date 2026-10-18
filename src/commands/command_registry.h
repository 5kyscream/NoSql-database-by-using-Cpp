#pragma once
#include "storage/db.h"
#include "protocol/resp_parser.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace minired::commands {

using Args    = std::vector<std::string>;
using Handler = std::function<std::string(const Args&, storage::DB&)>;

// Registry holds command name → handler mapping.
class CommandRegistry {
public:
    void register_all();
    std::string dispatch(const Args& args, storage::DB& db) const;

private:
    std::unordered_map<std::string, Handler> handlers_;
    void reg(const std::string& name, Handler h);
};

} // namespace minired::commands
