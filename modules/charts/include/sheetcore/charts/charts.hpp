#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/document/workbook.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sheetcore::charts {

using ChartId = std::uint32_t;

enum class ChartType {
    Column,
    Bar,
    Line,
    Area,
    Pie,
    Doughnut,
    Scatter,
    Bubble,
    Radar,
    Stock,
    Combo
};

struct ChartSeries {
    std::string name;
    SheetRangeAddress valuesRange{};
    std::optional<SheetRangeAddress> categoriesRange;
    std::optional<ChartType> overrideType; // for Combo charts
};

struct AxisConfig {
    std::string title;
    std::optional<double> minValue;
    std::optional<double> maxValue;
    bool logarithmic = false;
    bool showGridlines = true;
};

struct ChartPosition {
    CellCoord anchorTopLeft{};
    double widthPx = 480.0;
    double heightPx = 300.0;
};

struct ChartDef {
    ChartId id = 0;
    SheetId hostSheetId = kInvalidSheetId;
    ChartType type = ChartType::Column;
    std::string title;
    bool showLegend = true;
    bool showDataLabels = false;
    AxisConfig xAxis{};
    AxisConfig yAxis{};
    std::vector<ChartSeries> series;
    ChartPosition position{};
};

struct ResolvedSeriesData {
    std::string name;
    std::vector<std::string> categories;
    std::vector<double> values;
};

class ChartStore {
public:
    Result<ChartId> addChart(ChartDef def);
    bool removeChart(ChartId id);
    const ChartDef* findById(ChartId id) const noexcept;
    std::vector<const ChartDef*> chartsOnSheet(SheetId sheetId) const;

    static std::vector<ResolvedSeriesData> resolveChartData(const Workbook& wb, const ChartDef& chart);

private:
    ChartId nextId_ = 1;
    std::vector<ChartDef> charts_;
};

} // namespace sheetcore::charts
