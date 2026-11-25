#include "sheetcore/document/named_ranges.hpp"

#include <cctype>

namespace sheetcore {

std::string NamedRangeStore::makeKey(std::string_view name, SheetId scope) {
    std::string key;
    key.reserve(name.size() + 12);
    key += std::to_string(scope);
    key.push_back(':');
    for (char ch : name) {
        key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return key;
}

bool NamedRangeStore::define(NamedRangeDef def) {
    if (def.name.empty()) {
        return false;
    }
    std::string key = makeKey(def.name, def.scopeSheetId);
    entries_[std::move(key)] = std::move(def);
    return true;
}

bool NamedRangeStore::remove(std::string_view name, SheetId scopeSheetId) {
    return entries_.erase(makeKey(name, scopeSheetId)) > 0;
}

const NamedRangeDef* NamedRangeStore::resolve(std::string_view name, SheetId activeSheetId) const {
    if (activeSheetId != kInvalidSheetId) {
        auto it = entries_.find(makeKey(name, activeSheetId));
        if (it != entries_.end()) {
            return &it->second;
        }
    }
    auto it = entries_.find(makeKey(name, kInvalidSheetId));
    return it == entries_.end() ? nullptr : &it->second;
}

} // namespace sheetcore
