#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"
#include "sheetcore/document/worksheet.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sheetcore::data {

struct RangeOps {
    static Result<void> writeBlock(Worksheet& ws, CellCoord topLeft,
                                   const std::vector<std::vector<CellValue>>& rows);
    static std::vector<std::vector<CellValue>> readBlock(const Worksheet& ws, const RangeAddress& range);

    static Result<void> copyRange(const Worksheet& src, const RangeAddress& srcRange,
                                  Worksheet& dst, CellCoord dstTopLeft);
    static Result<void> moveRange(Worksheet& src, const RangeAddress& srcRange,
                                  Worksheet& dst, CellCoord dstTopLeft);

    static void clearContents(Worksheet& ws, const RangeAddress& range);
    static void clearStyles(Worksheet& ws, const RangeAddress& range);

    static Result<void> fillDown(Worksheet& ws, const RangeAddress& range);
    static Result<void> fillRight(Worksheet& ws, const RangeAddress& range);
    static Result<void> transpose(const Worksheet& src, const RangeAddress& srcRange,
                                  Worksheet& dst, CellCoord dstTopLeft);

    static std::size_t findAndReplace(Worksheet& ws, const RangeAddress& range,
                                      std::string_view findText, std::string_view replaceText,
                                      bool matchCase = false);

    static Result<void> insertRows(Worksheet& ws, RowIndex atRow, RowIndex count);
    static Result<void> deleteRows(Worksheet& ws, RowIndex atRow, RowIndex count);
    static Result<void> insertCols(Worksheet& ws, ColIndex atCol, ColIndex count);
    static Result<void> deleteCols(Worksheet& ws, ColIndex atCol, ColIndex count);
};

} // namespace sheetcore::data
