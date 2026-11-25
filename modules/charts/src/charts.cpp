#include "sheetcore/charts/charts.hpp"

#include <algorithm>

namespace sheetcore::charts {

Result<ChartId> ChartStore::addChart(ChartDef def) {
    def.id = nextId_++;
    ChartId id = def.id;
    charts_.push_back(std::move(def));
    return id;
}

bool ChartStore::removeChart(ChartId id) {
    auto it = std::find_if(charts_.begin(), charts_.end(), [id](const ChartDef& c) { return c.id == id; });
    if (it == charts_.end()) return false;
    charts_.erase(it);
    return true;
}

const ChartDef* ChartStore::findById(ChartId id) const noexcept {
    for (const auto& c : charts_) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

std::vector<const ChartDef*> ChartStore::chartsOnSheet(SheetId sheetId) const {
    std::vector<const ChartDef*> out;
    for (const auto& c : charts_) {
        if (c.hostSheetId == sheetId) out.push_back(&c);
    }
    return out;
}

std::vector<ResolvedSeriesData> ChartStore::resolveChartData(const Workbook& wb, const ChartDef& chart) {
    std::vector<ResolvedSeriesData> out;
    out.reserve(chart.series.size());

    for (const auto& s : chart.series) {
        ResolvedSeriesData rs;
        rs.name = s.name;

        if (const Worksheet* ws = wb.getSheet(s.valuesRange.sheetId)) {
            auto n = s.valuesRange.range.normalized();
            for (RowIndex r = n.start.row; r <= n.end.row; ++r) {
                for (ColIndex c = n.start.col; c <= n.end.col; ++c) {
                    auto num = toNumber(ws->getValue(CellCoord{r, c}));
                    rs.values.push_back(num.value_or(0.0));
                }
            }
        }

        if (s.categoriesRange.has_value()) {
            if (const Worksheet* cws = wb.getSheet(s.categoriesRange->sheetId)) {
                auto cn = s.categoriesRange->range.normalized();
                for (RowIndex r = cn.start.row; r <= cn.end.row; ++r) {
                    for (ColIndex c = cn.start.col; c <= cn.end.col; ++c) {
                        rs.categories.push_back(toDisplayString(cws->getValue(CellCoord{r, c})));
                    }
                }
            }
        }

        out.push_back(std::move(rs));
    }
    return out;
}

} // namespace sheetcore::charts
