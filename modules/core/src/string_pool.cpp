#include "sheetcore/core/string_pool.hpp"

namespace sheetcore {

StringPool::StringPool() {
    strings_.emplace_back("");
    index_.emplace(std::string_view(strings_.front()), kEmptyStringId);
}

StringId StringPool::intern(std::string_view text) {
    if (text.empty()) {
        return kEmptyStringId;
    }
    auto it = index_.find(text);
    if (it != index_.end()) {
        return it->second;
    }
    StringId id = static_cast<StringId>(strings_.size());
    strings_.emplace_back(text);
    index_.emplace(std::string_view(strings_.back()), id);
    return id;
}

std::string_view StringPool::get(StringId id) const noexcept {
    if (id >= strings_.size()) {
        return {};
    }
    return strings_[id];
}

} // namespace sheetcore
