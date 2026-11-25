#pragma once

#include "sheetcore/core/types.hpp"

#include <optional>
#include <string>

namespace sheetcore {

enum class WorkbookEventType {
    CellValueChanged,
    CellFormulaChanged,
    CellStyleChanged,
    RangeChanged,
    FormulaRecalculated,
    SheetAdded,
    SheetRemoved,
    SheetRenamed,
    NamedRangeChanged,
    CalculationCompleted
};

struct WorkbookEvent {
    WorkbookEventType type{};
    SheetId sheetId = kInvalidSheetId;
    std::optional<RangeAddress> range;
    std::string name;
};

class IWorkbookObserver {
public:
    virtual ~IWorkbookObserver() = default;
    virtual void onWorkbookEvent(const WorkbookEvent& event) = 0;
};

} // namespace sheetcore
