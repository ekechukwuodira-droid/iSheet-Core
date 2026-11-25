#pragma once

#include "sheetcore/core/value.hpp"
#include "sheetcore/format/style.hpp"

#include <string>

namespace sheetcore::format {

class NumberFormatter {
public:
    static std::string format(const CellValue& value, const NumberFormat& fmt);
};

} // namespace sheetcore::format
