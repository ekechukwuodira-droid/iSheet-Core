#pragma once

#include "sheetcore/core/events.hpp"
#include "sheetcore/core/result.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/document/named_ranges.hpp"
#include "sheetcore/document/worksheet.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sheetcore {

enum class CalculationMode {
    Automatic,
    Manual,
    AutomaticExceptTables
};

class Workbook {
public:
    Workbook() = default;
    ~Workbook() = default;

    Workbook(const Workbook&) = delete;
    Workbook& operator=(const Workbook&) = delete;
    Workbook(Workbook&& other) noexcept;
    Workbook& operator=(Workbook&& other) noexcept;

    static Workbook create();

    // Worksheet management
    Result<Worksheet*> addSheet(std::string name);
    bool removeSheet(SheetId id);
    bool renameSheet(SheetId id, std::string newName);

    Worksheet* getSheet(SheetId id) noexcept;
    const Worksheet* getSheet(SheetId id) const noexcept;
    Worksheet* getSheet(std::string_view name) noexcept;
    const Worksheet* getSheet(std::string_view name) const noexcept;

    std::vector<SheetId> sheetIds() const;
    std::size_t sheetCount() const noexcept { return sheets_.size(); }

    // Calculation settings
    void setCalculationMode(CalculationMode mode) noexcept { calcMode_ = mode; }
    CalculationMode calculationMode() const noexcept { return calcMode_; }

    // Dirty/saved state
    bool isModified() const noexcept { return modified_; }
    void markModified() noexcept { modified_ = true; }
    void markSaved() noexcept { modified_ = false; }

    // Named ranges
    NamedRangeStore& namedRanges() noexcept { return namedRanges_; }
    const NamedRangeStore& namedRanges() const noexcept { return namedRanges_; }

    // Observer notifications (used by calc, condfmt, UI, etc.)
    void addObserver(IWorkbookObserver* observer);
    void removeObserver(IWorkbookObserver* observer);
    void notify(const WorkbookEvent& event);

private:
    SheetId nextSheetId_ = 1;
    std::vector<std::unique_ptr<Worksheet>> sheets_;
    CalculationMode calcMode_ = CalculationMode::Automatic;
    bool modified_ = false;
    NamedRangeStore namedRanges_;
    std::vector<IWorkbookObserver*> observers_;
};

} // namespace sheetcore
