#pragma once

#include "sheetcore/commands/command.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"
#include "sheetcore/document/cell.hpp"

#include <optional>
#include <string>

namespace sheetcore::commands {

struct CellSnapshot {
    bool existed = false;
    CellValue value = EmptyValue{};
    std::optional<std::string> formulaText;
    StyleId styleId = kDefaultStyleId;

    static CellSnapshot capture(const Worksheet& ws, CellCoord coord);
    Result<void> restore(Worksheet& ws, CellCoord coord) const;
};

class SetCellValueCommand final : public ICommand {
public:
    SetCellValueCommand(SheetId sheetId, CellCoord coord, CellValue newValue);

    std::string description() const override { return "Set Cell Value"; }
    Result<void> execute(Workbook& wb) override;
    Result<void> undo(Workbook& wb) override;

private:
    SheetId sheetId_;
    CellCoord coord_;
    CellValue newValue_;
    CellSnapshot prev_{};
};

class SetCellFormulaCommand final : public ICommand {
public:
    SetCellFormulaCommand(SheetId sheetId, CellCoord coord, std::string formula);

    std::string description() const override { return "Set Cell Formula"; }
    Result<void> execute(Workbook& wb) override;
    Result<void> undo(Workbook& wb) override;

private:
    SheetId sheetId_;
    CellCoord coord_;
    std::string formula_;
    CellSnapshot prev_{};
};

class SetCellStyleCommand final : public ICommand {
public:
    SetCellStyleCommand(SheetId sheetId, CellCoord coord, StyleId styleId);

    std::string description() const override { return "Set Cell Style"; }
    Result<void> execute(Workbook& wb) override;
    Result<void> undo(Workbook& wb) override;

private:
    SheetId sheetId_;
    CellCoord coord_;
    StyleId styleId_;
    CellSnapshot prev_{};
};

class ClearCellCommand final : public ICommand {
public:
    ClearCellCommand(SheetId sheetId, CellCoord coord);

    std::string description() const override { return "Clear Cell"; }
    Result<void> execute(Workbook& wb) override;
    Result<void> undo(Workbook& wb) override;

private:
    SheetId sheetId_;
    CellCoord coord_;
    CellSnapshot prev_{};
};

class RenameSheetCommand final : public ICommand {
public:
    RenameSheetCommand(SheetId sheetId, std::string newName);

    std::string description() const override { return "Rename Sheet"; }
    Result<void> execute(Workbook& wb) override;
    Result<void> undo(Workbook& wb) override;

private:
    SheetId sheetId_;
    std::string newName_;
    std::string oldName_;
};

} // namespace sheetcore::commands
