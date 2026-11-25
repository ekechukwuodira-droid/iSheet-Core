#include "sheetcore/commands/basic_commands.hpp"

namespace sheetcore::commands {

CellSnapshot CellSnapshot::capture(const Worksheet& ws, CellCoord coord) {
    CellSnapshot snap;
    if (const CellRecord* rec = ws.getCell(coord)) {
        snap.existed = true;
        snap.value = rec->value;
        snap.formulaText = rec->formulaText;
        snap.styleId = rec->styleId;
    }
    return snap;
}

Result<void> CellSnapshot::restore(Worksheet& ws, CellCoord coord) const {
    if (!existed) {
        return ws.clearCell(coord);
    }
    if (formulaText.has_value()) {
        auto r = ws.setFormula(coord, *formulaText);
        if (!r) return r;
        ws.setCachedValue(coord, value);
    } else {
        auto r = ws.setValue(coord, value);
        if (!r) return r;
    }
    return ws.setStyle(coord, styleId);
}

SetCellValueCommand::SetCellValueCommand(SheetId sheetId, CellCoord coord, CellValue newValue)
    : sheetId_(sheetId), coord_(coord), newValue_(std::move(newValue)) {}

Result<void> SetCellValueCommand::execute(Workbook& wb) {
    Worksheet* ws = wb.getSheet(sheetId_);
    if (!ws) return Error{ErrorCode::NotFound, "Sheet not found"};
    prev_ = CellSnapshot::capture(*ws, coord_);
    return ws->setValue(coord_, newValue_);
}

Result<void> SetCellValueCommand::undo(Workbook& wb) {
    Worksheet* ws = wb.getSheet(sheetId_);
    if (!ws) return Error{ErrorCode::NotFound, "Sheet not found"};
    return prev_.restore(*ws, coord_);
}

SetCellFormulaCommand::SetCellFormulaCommand(SheetId sheetId, CellCoord coord, std::string formula)
    : sheetId_(sheetId), coord_(coord), formula_(std::move(formula)) {}

Result<void> SetCellFormulaCommand::execute(Workbook& wb) {
    Worksheet* ws = wb.getSheet(sheetId_);
    if (!ws) return Error{ErrorCode::NotFound, "Sheet not found"};
    prev_ = CellSnapshot::capture(*ws, coord_);
    return ws->setFormula(coord_, formula_);
}

Result<void> SetCellFormulaCommand::undo(Workbook& wb) {
    Worksheet* ws = wb.getSheet(sheetId_);
    if (!ws) return Error{ErrorCode::NotFound, "Sheet not found"};
    return prev_.restore(*ws, coord_);
}

SetCellStyleCommand::SetCellStyleCommand(SheetId sheetId, CellCoord coord, StyleId styleId)
    : sheetId_(sheetId), coord_(coord), styleId_(styleId) {}

Result<void> SetCellStyleCommand::execute(Workbook& wb) {
    Worksheet* ws = wb.getSheet(sheetId_);
    if (!ws) return Error{ErrorCode::NotFound, "Sheet not found"};
    prev_ = CellSnapshot::capture(*ws, coord_);
    return ws->setStyle(coord_, styleId_);
}

Result<void> SetCellStyleCommand::undo(Workbook& wb) {
    Worksheet* ws = wb.getSheet(sheetId_);
    if (!ws) return Error{ErrorCode::NotFound, "Sheet not found"};
    return prev_.restore(*ws, coord_);
}

ClearCellCommand::ClearCellCommand(SheetId sheetId, CellCoord coord)
    : sheetId_(sheetId), coord_(coord) {}

Result<void> ClearCellCommand::execute(Workbook& wb) {
    Worksheet* ws = wb.getSheet(sheetId_);
    if (!ws) return Error{ErrorCode::NotFound, "Sheet not found"};
    prev_ = CellSnapshot::capture(*ws, coord_);
    return ws->clearCell(coord_);
}

Result<void> ClearCellCommand::undo(Workbook& wb) {
    Worksheet* ws = wb.getSheet(sheetId_);
    if (!ws) return Error{ErrorCode::NotFound, "Sheet not found"};
    return prev_.restore(*ws, coord_);
}

RenameSheetCommand::RenameSheetCommand(SheetId sheetId, std::string newName)
    : sheetId_(sheetId), newName_(std::move(newName)) {}

Result<void> RenameSheetCommand::execute(Workbook& wb) {
    Worksheet* ws = wb.getSheet(sheetId_);
    if (!ws) return Error{ErrorCode::NotFound, "Sheet not found"};
    oldName_ = ws->name();
    if (!wb.renameSheet(sheetId_, newName_)) {
        return Error{ErrorCode::InvalidArgument, "Failed to rename sheet"};
    }
    return Result<void>::ok();
}

Result<void> RenameSheetCommand::undo(Workbook& wb) {
    if (!wb.renameSheet(sheetId_, oldName_)) {
        return Error{ErrorCode::InvalidArgument, "Failed to restore sheet name"};
    }
    return Result<void>::ok();
}

} // namespace sheetcore::commands
