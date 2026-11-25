#include "sheetcore/func_lookup_date/register.hpp"

#include <chrono>
#include <cmath>
#include <ctime>

namespace sheetcore::func_lookup_date {

namespace {

// Civil date (y, m, d) -> Excel serial days since 1899-12-30
double civilToSerial(long long y, unsigned m, unsigned d) {
    y -= (m <= 2);
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long daysSince1970 = era * 146097 + static_cast<long long>(doe) - 719468;
    return static_cast<double>(daysSince1970 + 25569LL);
}

void serialToCivil(double serial, int& year, int& month, int& day) {
    long long daysSince1970 = static_cast<long long>(std::floor(serial)) - 25569LL;
    long long z = daysSince1970 + 719468LL;
    long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    unsigned doe = static_cast<unsigned>(z - era * 146097LL);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = static_cast<long long>(yoe) + era * 400LL;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    year = static_cast<int>(y);
    month = static_cast<int>(m);
    day = static_cast<int>(d);
}

CellValue fnDate(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto y = toNumber(args[0].scalar);
    auto m = toNumber(args[1].scalar);
    auto d = toNumber(args[2].scalar);
    if (!y || !m || !d) return ErrorValue{ErrorType::Value};
    return DateTimeValue{civilToSerial(static_cast<long long>(*y),
                                       static_cast<unsigned>(*m),
                                       static_cast<unsigned>(*d))};
}

CellValue fnYear(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto s = toNumber(args[0].scalar);
    if (!s) return ErrorValue{ErrorType::Value};
    int y = 0, m = 0, d = 0;
    serialToCivil(*s, y, m, d);
    return static_cast<double>(y);
}

CellValue fnMonth(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto s = toNumber(args[0].scalar);
    if (!s) return ErrorValue{ErrorType::Value};
    int y = 0, m = 0, d = 0;
    serialToCivil(*s, y, m, d);
    return static_cast<double>(m);
}

CellValue fnDay(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto s = toNumber(args[0].scalar);
    if (!s) return ErrorValue{ErrorType::Value};
    int y = 0, m = 0, d = 0;
    serialToCivil(*s, y, m, d);
    return static_cast<double>(d);
}

CellValue fnToday(const std::vector<formula::EvalArg>&, const formula::EvalContext&) {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmVal{};
    localtime_s(&tmVal, &tt);
    return DateTimeValue{civilToSerial(tmVal.tm_year + 1900,
                                       static_cast<unsigned>(tmVal.tm_mon + 1),
                                       static_cast<unsigned>(tmVal.tm_mday))};
}

CellValue fnNow(const std::vector<formula::EvalArg>&, const formula::EvalContext&) {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmVal{};
    localtime_s(&tmVal, &tt);
    double daySerial = civilToSerial(tmVal.tm_year + 1900,
                                     static_cast<unsigned>(tmVal.tm_mon + 1),
                                     static_cast<unsigned>(tmVal.tm_mday));
    double frac = (tmVal.tm_hour * 3600.0 + tmVal.tm_min * 60.0 + tmVal.tm_sec) / 86400.0;
    return DateTimeValue{daySerial + frac};
}

CellValue fnVLookup(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (!args[1].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
    auto colIdxOpt = toNumber(args[2].scalar);
    if (!colIdxOpt || *colIdxOpt < 1.0) return ErrorValue{ErrorType::Value};
    ColIndex colIdx = static_cast<ColIndex>(*colIdxOpt) - 1;

    SheetId sid = args[1].rangeRef->sheetId;
    RangeAddress rng = args[1].rangeRef->range.normalized();
    if (colIdx >= rng.colCount()) return ErrorValue{ErrorType::Ref};

    const CellValue& needle = args[0].scalar;
    for (RowIndex r = rng.start.row; r <= rng.end.row; ++r) {
        if (ctx.values->getCellValue(sid, CellCoord{r, rng.start.col}) == needle) {
            return ctx.values->getCellValue(sid, CellCoord{r, rng.start.col + colIdx});
        }
    }
    return ErrorValue{ErrorType::NA};
}

CellValue fnHLookup(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (!args[1].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
    auto rowIdxOpt = toNumber(args[2].scalar);
    if (!rowIdxOpt || *rowIdxOpt < 1.0) return ErrorValue{ErrorType::Value};
    RowIndex rowIdx = static_cast<RowIndex>(*rowIdxOpt) - 1;

    SheetId sid = args[1].rangeRef->sheetId;
    RangeAddress rng = args[1].rangeRef->range.normalized();
    if (rowIdx >= rng.rowCount()) return ErrorValue{ErrorType::Ref};

    const CellValue& needle = args[0].scalar;
    for (ColIndex c = rng.start.col; c <= rng.end.col; ++c) {
        if (ctx.values->getCellValue(sid, CellCoord{rng.start.row, c}) == needle) {
            return ctx.values->getCellValue(sid, CellCoord{rng.start.row + rowIdx, c});
        }
    }
    return ErrorValue{ErrorType::NA};
}

CellValue fnXLookup(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (!args[1].isRange() || !args[2].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
    auto lookupVals = ctx.values->getRangeValues(args[1].rangeRef->sheetId, args[1].rangeRef->range);
    auto returnVals = ctx.values->getRangeValues(args[2].rangeRef->sheetId, args[2].rangeRef->range);
    if (lookupVals.size() != returnVals.size()) return ErrorValue{ErrorType::Value};

    const CellValue& needle = args[0].scalar;
    for (std::size_t i = 0; i < lookupVals.size(); ++i) {
        if (lookupVals[i] == needle) {
            return returnVals[i];
        }
    }
    if (args.size() >= 4) {
        return args[3].scalar; // if_not_found
    }
    return ErrorValue{ErrorType::NA};
}

CellValue fnIndex(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto rowOpt = toNumber(args[1].scalar);
    if (!rowOpt || *rowOpt < 1.0) return ErrorValue{ErrorType::Value};
    RowIndex rOff = static_cast<RowIndex>(*rowOpt) - 1;
    ColIndex cOff = 0;
    if (args.size() >= 3 && !args[2].isOmitted()) {
        auto colOpt = toNumber(args[2].scalar);
        if (!colOpt || *colOpt < 1.0) return ErrorValue{ErrorType::Value};
        cOff = static_cast<ColIndex>(*colOpt) - 1;
    }
    if (args[0].isArray()) {
        const auto& arr = *args[0].array;
        if (rOff >= arr.rows || cOff >= arr.cols) return ErrorValue{ErrorType::Ref};
        return arr.at(rOff, cOff);
    }
    if (!args[0].isRange() || !ctx.values) {
        return (rOff == 0 && cOff == 0) ? args[0].scalar : CellValue{ErrorValue{ErrorType::Ref}};
    }
    RangeAddress rng = args[0].rangeRef->range.normalized();
    if (rOff >= rng.rowCount() || cOff >= rng.colCount()) return ErrorValue{ErrorType::Ref};
    return ctx.values->getCellValue(args[0].rangeRef->sheetId,
                                    CellCoord{rng.start.row + rOff, rng.start.col + cOff});
}

CellValue fnMatch(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (!args[1].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
    auto vals = ctx.values->getRangeValues(args[1].rangeRef->sheetId, args[1].rangeRef->range);
    const CellValue& needle = args[0].scalar;
    for (std::size_t i = 0; i < vals.size(); ++i) {
        if (vals[i] == needle) {
            return static_cast<double>(i + 1);
        }
    }
    return ErrorValue{ErrorType::NA};
}

} // namespace

void registerLookupDateFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"DATE", 3, 3, fnDate});
    registry.registerFunction({"YEAR", 1, 1, fnYear});
    registry.registerFunction({"MONTH", 1, 1, fnMonth});
    registry.registerFunction({"DAY", 1, 1, fnDay});
    registry.registerFunction({"TODAY", 0, 0, fnToday});
    registry.registerFunction({"NOW", 0, 0, fnNow});
    registry.registerFunction({"VLOOKUP", 3, 4, fnVLookup});
    registry.registerFunction({"HLOOKUP", 3, 4, fnHLookup});
    registry.registerFunction({"XLOOKUP", 3, 4, fnXLookup});
    registry.registerFunction({"INDEX", 2, 3, fnIndex});
    registry.registerFunction({"MATCH", 2, 3, fnMatch});
}

} // namespace sheetcore::func_lookup_date
