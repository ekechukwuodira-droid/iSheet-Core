#include "sheetcore/document/worksheet.hpp"
#include "sheetcore/document/workbook.hpp"

namespace sheetcore {

Worksheet::Worksheet(Workbook& owner, SheetId id, std::string name)
    : owner_(&owner), id_(id), name_(std::move(name)) {}

Result<void> Worksheet::setValue(CellCoord coord, CellValue value) {
    CellRecord& rec = tiles_.getOrCreate(coord);
    rec.value = std::move(value);
    rec.formulaText.reset();
    rec.formulaId.reset();
    tiles_.removeIfEmpty(coord);

    owner_->markModified();
    owner_->notify(WorkbookEvent{
        WorkbookEventType::CellValueChanged,
        id_,
        RangeAddress{coord, coord},
        {}
    });
    return Result<void>::ok();
}

Result<void> Worksheet::setValue(std::string_view a1, CellValue value) {
    auto coord = CellCoord::fromA1(a1);
    if (!coord) {
        return Error{ErrorCode::InvalidArgument, "Invalid cell address: " + std::string(a1)};
    }
    return setValue(*coord, std::move(value));
}

Result<void> Worksheet::setFormula(CellCoord coord, std::string formula) {
    if (formula.empty() || formula.front() != '=') {
        return Error{ErrorCode::InvalidArgument, "Formula must start with '='"};
    }
    CellRecord& rec = tiles_.getOrCreate(coord);
    rec.formulaText = std::move(formula);
    rec.formulaId.reset();

    owner_->markModified();
    owner_->notify(WorkbookEvent{
        WorkbookEventType::CellFormulaChanged,
        id_,
        RangeAddress{coord, coord},
        {}
    });
    return Result<void>::ok();
}

Result<void> Worksheet::setFormula(std::string_view a1, std::string formula) {
    auto coord = CellCoord::fromA1(a1);
    if (!coord) {
        return Error{ErrorCode::InvalidArgument, "Invalid cell address: " + std::string(a1)};
    }
    return setFormula(*coord, std::move(formula));
}

void Worksheet::setCachedValue(CellCoord coord, CellValue value, std::optional<FormulaId> formulaId) {
    CellRecord& rec = tiles_.getOrCreate(coord);
    rec.value = std::move(value);
    if (formulaId.has_value()) {
        rec.formulaId = formulaId;
    }
    owner_->notify(WorkbookEvent{
        WorkbookEventType::FormulaRecalculated,
        id_,
        RangeAddress{coord, coord},
        {}
    });
}

Result<void> Worksheet::setStyle(CellCoord coord, StyleId styleId) {
    CellRecord& rec = tiles_.getOrCreate(coord);
    rec.styleId = styleId;
    tiles_.removeIfEmpty(coord);

    owner_->markModified();
    owner_->notify(WorkbookEvent{
        WorkbookEventType::CellStyleChanged,
        id_,
        RangeAddress{coord, coord},
        {}
    });
    return Result<void>::ok();
}

Result<void> Worksheet::clearCell(CellCoord coord) {
    if (tiles_.clearCell(coord)) {
        owner_->markModified();
        owner_->notify(WorkbookEvent{
            WorkbookEventType::CellValueChanged,
            id_,
            RangeAddress{coord, coord},
            {}
        });
    }
    return Result<void>::ok();
}

Result<void> Worksheet::clearCell(std::string_view a1) {
    auto coord = CellCoord::fromA1(a1);
    if (!coord) {
        return Error{ErrorCode::InvalidArgument, "Invalid cell address: " + std::string(a1)};
    }
    return clearCell(*coord);
}

CellValue Worksheet::getValue(CellCoord coord) const {
    const CellRecord* rec = tiles_.get(coord);
    return rec ? rec->value : CellValue{EmptyValue{}};
}

CellValue Worksheet::getValue(std::string_view a1) const {
    auto coord = CellCoord::fromA1(a1);
    if (!coord) {
        return ErrorValue{ErrorType::Ref};
    }
    return getValue(*coord);
}

const CellRecord* Worksheet::getCell(CellCoord coord) const noexcept {
    return tiles_.get(coord);
}

const CellRecord* Worksheet::getCell(std::string_view a1) const noexcept {
    auto coord = CellCoord::fromA1(a1);
    if (!coord) {
        return nullptr;
    }
    return tiles_.get(*coord);
}

void Worksheet::forEachCell(const std::function<void(CellCoord, const CellRecord&)>& fn) const {
    tiles_.forEachCell(fn);
}

bool Worksheet::mergeCells(const RangeAddress& range) {
    bool ok = merged_.add(range);
    if (ok) {
        owner_->markModified();
    }
    return ok;
}

bool Worksheet::unmergeCells(const RangeAddress& range) {
    bool ok = merged_.remove(range);
    if (ok) {
        owner_->markModified();
    }
    return ok;
}

} // namespace sheetcore
