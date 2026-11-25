#pragma once

#include "sheetcore/core/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sheetcore {

struct NamedRangeDef {
    std::string name;
    SheetId scopeSheetId = kInvalidSheetId; // kInvalidSheetId => workbook-level
    std::string expression;                // e.g., "A1:B10" or "=0.08"
};

class NamedRangeStore {
public:
    bool define(NamedRangeDef def);
    bool remove(std::string_view name, SheetId scopeSheetId = kInvalidSheetId);
    const NamedRangeDef* resolve(std::string_view name, SheetId activeSheetId = kInvalidSheetId) const;
    const std::unordered_map<std::string, NamedRangeDef>& all() const noexcept { return entries_; }

private:
    static std::string makeKey(std::string_view name, SheetId scope);
    std::unordered_map<std::string, NamedRangeDef> entries_;
};

} // namespace sheetcore
