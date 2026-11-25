#pragma once

#include "sheetcore/core/events.hpp"
#include "sheetcore/core/result.hpp"
#include "sheetcore/core/string_pool.hpp"
#include "sheetcore/core/thread_pool.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"

#include "sheetcore/document/cell.hpp"
#include "sheetcore/document/named_ranges.hpp"
#include "sheetcore/document/tile_storage.hpp"
#include "sheetcore/document/workbook.hpp"
#include "sheetcore/document/worksheet.hpp"

#include "sheetcore/formula/ast.hpp"
#include "sheetcore/formula/context.hpp"
#include "sheetcore/formula/evaluator.hpp"
#include "sheetcore/formula/functions.hpp"
#include "sheetcore/formula/parser.hpp"
#include "sheetcore/formula/ref_transformer.hpp"

#include "sheetcore/func_core/register.hpp"
#include "sheetcore/func_lookup_date/register.hpp"
#include "sheetcore/func_stats_fin/register.hpp"
#include "sheetcore/func_dynarray/dynarray.hpp"
#include "sheetcore/func_advanced/register.hpp"
#include "sheetcore/func_everyday/register.hpp"
#include "sheetcore/func_math_trig/register.hpp"
#include "sheetcore/func_stats/register.hpp"
#include "sheetcore/func_financial/register.hpp"
#include "sheetcore/func_engineering/register.hpp"
#include "sheetcore/func_text_date_info/register.hpp"

#include "sheetcore/calc/calculation_engine.hpp"
#include "sheetcore/format/number_formatter.hpp"
#include "sheetcore/format/style.hpp"
#include "sheetcore/commands/basic_commands.hpp"
#include "sheetcore/commands/command.hpp"
#include "sheetcore/data/filtering.hpp"
#include "sheetcore/data/range_ops.hpp"
#include "sheetcore/data/sorting.hpp"
#include "sheetcore/data/tables.hpp"
#include "sheetcore/validation/validation.hpp"
#include "sheetcore/condfmt/condfmt.hpp"
#include "sheetcore/charts/charts.hpp"
#include "sheetcore/pivot/pivot.hpp"
#include "sheetcore/storage/storage.hpp"
#include "sheetcore/io_csv/csv_codec.hpp"
#include "sheetcore/io_isht/isht_codec.hpp"
#include "sheetcore/io_xlsx/xlsx_codec.hpp"

#include <filesystem>
#include <memory>

namespace sheetcore {

class SheetCoreEngine {
public:
    SheetCoreEngine();
    ~SheetCoreEngine() = default;

    SheetCoreEngine(const SheetCoreEngine&) = delete;
    SheetCoreEngine& operator=(const SheetCoreEngine&) = delete;

    Workbook& workbook() noexcept { return workbook_; }
    const Workbook& workbook() const noexcept { return workbook_; }

    formula::FunctionRegistry& functions() noexcept { return functions_; }
    calc::CalculationEngine& calc() noexcept { return *calc_; }
    format::StyleTable& styles() noexcept { return styles_; }
    commands::CommandHistory& history() noexcept { return history_; }
    data::TableStore& tables() noexcept { return tables_; }
    validation::ValidationEngine& validation() noexcept { return validation_; }
    condfmt::ConditionalFormatEngine& conditionalFormatting() noexcept { return condFmt_; }
    charts::ChartStore& charts() noexcept { return charts_; }
    func_dynarray::SpillManager& spills() noexcept { return spills_; }
    storage::StorageManager& storage() noexcept { return storage_; }

    calc::CalculationStats recalculate() { return calc_->recalculate(); }
    Result<void> save(const std::filesystem::path& path) const { return storage_.save(workbook_, path); }
    Result<void> load(const std::filesystem::path& path);

private:
    class TableResolverAdapter final : public formula::ITableResolver {
    public:
        explicit TableResolverAdapter(const data::TableStore& store) : store_(store) {}
        std::optional<SheetRangeAddress> resolveTableColumn(std::string_view tableName,
                                                            std::string_view columnName) const override {
            return store_.resolveColumnRange(tableName, columnName);
        }
    private:
        const data::TableStore& store_;
    };

    Workbook workbook_;
    formula::FunctionRegistry functions_;
    data::TableStore tables_;
    TableResolverAdapter tableResolver_;
    std::unique_ptr<calc::CalculationEngine> calc_;
    format::StyleTable styles_;
    commands::CommandHistory history_;
    validation::ValidationEngine validation_;
    condfmt::ConditionalFormatEngine condFmt_;
    charts::ChartStore charts_;
    func_dynarray::SpillManager spills_;
    storage::StorageManager storage_;
};

} // namespace sheetcore
