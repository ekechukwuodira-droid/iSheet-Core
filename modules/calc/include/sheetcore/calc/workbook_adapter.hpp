#pragma once

#include "sheetcore/document/workbook.hpp"
#include "sheetcore/formula/context.hpp"

namespace sheetcore::calc {

class WorkbookValueProvider final : public formula::ICellValueProvider, public formula::INameResolver {
public:
    explicit WorkbookValueProvider(const Workbook& wb) : wb_(wb) {}

    std::optional<SheetId> resolveSheetId(std::string_view sheetName) const override {
        const Worksheet* ws = wb_.getSheet(sheetName);
        return ws ? std::optional<SheetId>(ws->id()) : std::nullopt;
    }

    CellValue getCellValue(SheetId sheetId, CellCoord coord) const override {
        const Worksheet* ws = wb_.getSheet(sheetId);
        return ws ? ws->getValue(coord) : CellValue{ErrorValue{ErrorType::Ref}};
    }

    std::vector<CellValue> getRangeValues(SheetId sheetId, const RangeAddress& range) const override {
        std::vector<CellValue> out;
        const Worksheet* ws = wb_.getSheet(sheetId);
        if (!ws) {
            out.push_back(ErrorValue{ErrorType::Ref});
            return out;
        }
        auto n = range.normalized();
        // Fast path: iterate sparse cells if range is huge, or direct for moderate ranges
        std::uint64_t total = n.cellCount();
        if (total <= 4096) {
            out.reserve(static_cast<std::size_t>(total));
            for (RowIndex r = n.start.row; r <= n.end.row; ++r) {
                for (ColIndex c = n.start.col; c <= n.end.col; ++c) {
                    out.push_back(ws->getValue(CellCoord{r, c}));
                }
            }
        } else {
            ws->forEachCell([&](CellCoord c, const CellRecord& rec) {
                if (n.contains(c)) {
                    out.push_back(rec.value);
                }
            });
        }
        return out;
    }

    std::optional<std::string> resolveNameExpression(std::string_view name, SheetId activeSheetId) const override {
        const NamedRangeDef* def = wb_.namedRanges().resolve(name, activeSheetId);
        return def ? std::optional<std::string>(def->expression) : std::nullopt;
    }

    std::optional<std::string> getCellFormula(SheetId sheetId, CellCoord coord) const override {
        const Worksheet* ws = wb_.getSheet(sheetId);
        if (!ws) return std::nullopt;
        const CellRecord* rec = ws->getCell(coord);
        return (rec && rec->formulaText.has_value()) ? rec->formulaText : std::nullopt;
    }

    std::vector<std::string> sheetNames() const override {
        std::vector<std::string> out;
        for (SheetId sid : wb_.sheetIds()) {
            if (const Worksheet* ws = wb_.getSheet(sid)) {
                out.push_back(ws->name());
            }
        }
        return out;
    }

    std::optional<std::string> sheetName(SheetId sheetId) const override {
        const Worksheet* ws = wb_.getSheet(sheetId);
        return ws ? std::optional<std::string>(ws->name()) : std::nullopt;
    }

    std::optional<RangeAddress> usedRange(SheetId sheetId) const override {
        const Worksheet* ws = wb_.getSheet(sheetId);
        return ws ? ws->usedRange() : std::nullopt;
    }

private:
    const Workbook& wb_;
};

} // namespace sheetcore::calc
