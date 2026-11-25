#include "sheetcore/sheetcore.hpp"

namespace sheetcore {

SheetCoreEngine::SheetCoreEngine()
    : workbook_(Workbook::create()),
      tableResolver_(tables_),
      history_(workbook_) {
    func_core::registerCoreFunctions(functions_);
    func_lookup_date::registerLookupDateFunctions(functions_);
    func_stats_fin::registerStatsFinFunctions(functions_);
    func_dynarray::registerDynArrayFunctions(functions_);
    func_advanced::registerAdvancedFunctions(functions_);
    func_everyday::registerEverydayFunctions(functions_);
    func_math_trig::registerMathTrigFunctions(functions_);
    func_stats::registerStatsFunctions(functions_);
    func_financial::registerFinancialFunctions(functions_);
    func_engineering::registerEngineeringFunctions(functions_);
    func_text_date_info::registerTextDateInfoFunctions(functions_);

    calc_ = std::make_unique<calc::CalculationEngine>(workbook_, functions_);
    calc_->setTableResolver(&tableResolver_);

    storage_.registerCodec(io::createIshtCodec());
    storage_.registerCodec(io::createCsvCodec(','));
    storage_.registerCodec(io::createCsvCodec('\t'));
    storage_.registerCodec(io::createXlsxCodec());
}

Result<void> SheetCoreEngine::load(const std::filesystem::path& path) {
    Workbook fresh = Workbook::create();
    auto res = storage_.load(fresh, path);
    if (!res) return res;

    calc_.reset();
    history_.clear();
    workbook_ = std::move(fresh);
    calc_ = std::make_unique<calc::CalculationEngine>(workbook_, functions_);
    calc_->setTableResolver(&tableResolver_);
    return Result<void>::ok();
}

} // namespace sheetcore
