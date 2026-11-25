#include "sheetcore/formula/array_utils.hpp"
#include "sheetcore/formula/functions.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace sheetcore::func_dynarray {

namespace {

int compareCellValues(const CellValue& a, const CellValue& b) {
    if (isNumber(a) && isNumber(b)) {
        double da = std::get<double>(a), db = std::get<double>(b);
        return (da < db) ? -1 : ((da > db) ? 1 : 0);
    }
    if (isBool(a) && isBool(b)) {
        bool ba = std::get<bool>(a), bb = std::get<bool>(b);
        return (ba == bb) ? 0 : (ba ? 1 : -1);
    }
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::string sa = lower(toDisplayString(a)), sb = lower(toDisplayString(b));
    return (sa < sb) ? -1 : ((sa > sb) ? 1 : 0);
}

// ---- FILTER(array, include, [if_empty]) ----
formula::EvalArg fnFilter(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    formula::ArrayValue inc = formula::toArray(a[1], ctx);
    bool byRows = (inc.rows == arr.rows && inc.cols == 1);
    bool byCols = (!byRows && inc.cols == arr.cols && inc.rows == 1);
    if (!byRows && !byCols) return formula::makeScalar(ErrorValue{ErrorType::Value});

    std::vector<std::size_t> kept;
    for (std::size_t i = 0; i < inc.elements.size(); ++i) {
        if (isError(inc.elements[i])) return formula::makeScalar(inc.elements[i]);
        if (auto b = toBool(inc.elements[i]); b && *b) kept.push_back(i);
    }
    if (kept.empty()) {
        if (a.size() >= 3 && !a[2].isOmitted()) return a[2];
        return formula::makeScalar(ErrorValue{ErrorType::Calc});
    }
    if (byRows) {
        formula::ArrayValue out(kept.size(), arr.cols);
        for (std::size_t r = 0; r < kept.size(); ++r) {
            for (std::size_t c = 0; c < arr.cols; ++c) out.at(r, c) = arr.at(kept[r], c);
        }
        return formula::makeArray(std::move(out));
    }
    formula::ArrayValue out(arr.rows, kept.size());
    for (std::size_t r = 0; r < arr.rows; ++r) {
        for (std::size_t c = 0; c < kept.size(); ++c) out.at(r, c) = arr.at(r, kept[c]);
    }
    return formula::makeArray(std::move(out));
}

// ---- SORTBY(array, by_array1, [sort_order1], ...) ----
formula::EvalArg fnSortBy(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    struct KeySpec {
        std::vector<CellValue> keys;
        bool asc = true;
    };
    std::vector<KeySpec> specs;
    for (std::size_t i = 1; i < a.size(); i += 2) {
        auto keys = formula::flatten(a[i], ctx);
        if (keys.size() != arr.rows) return formula::makeScalar(ErrorValue{ErrorType::Value});
        bool asc = true;
        if (i + 1 < a.size() && !a[i + 1].isOmitted()) {
            auto ord = toNumber(a[i + 1].scalar);
            if (!ord || (*ord != 1.0 && *ord != -1.0)) return formula::makeScalar(ErrorValue{ErrorType::Value});
            asc = (*ord >= 0.0);
        }
        specs.push_back(KeySpec{std::move(keys), asc});
    }
    std::vector<std::size_t> order(arr.rows);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t r1, std::size_t r2) {
        for (const auto& s : specs) {
            int cmp = compareCellValues(s.keys[r1], s.keys[r2]);
            if (cmp != 0) return s.asc ? (cmp < 0) : (cmp > 0);
        }
        return false;
    });
    formula::ArrayValue out(arr.rows, arr.cols);
    for (std::size_t r = 0; r < arr.rows; ++r) {
        for (std::size_t c = 0; c < arr.cols; ++c) out.at(r, c) = arr.at(order[r], c);
    }
    return formula::makeArray(std::move(out));
}

// ---- RANDARRAY([rows], [columns], [min], [max], [whole_number]) ----
formula::EvalArg fnRandArray(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    long long rows = (a.size() >= 1 && !a[0].isOmitted()) ? static_cast<long long>(std::trunc(toNumber(a[0].scalar).value_or(-1.0))) : 1;
    long long cols = (a.size() >= 2 && !a[1].isOmitted()) ? static_cast<long long>(std::trunc(toNumber(a[1].scalar).value_or(-1.0))) : 1;
    double mn = (a.size() >= 3 && !a[2].isOmitted()) ? toNumber(a[2].scalar).value_or(std::nan("")) : 0.0;
    double mx = (a.size() >= 4 && !a[3].isOmitted()) ? toNumber(a[3].scalar).value_or(std::nan("")) : 1.0;
    bool whole = (a.size() >= 5 && !a[4].isOmitted()) ? toBool(a[4].scalar).value_or(false) : false;
    if (rows < 1 || cols < 1 || rows * cols > 100000 || std::isnan(mn) || std::isnan(mx) || mn > mx) {
        return formula::makeScalar(ErrorValue{ErrorType::Value});
    }
    thread_local std::mt19937_64 rng{std::random_device{}()};
    formula::ArrayValue out(static_cast<std::size_t>(rows), static_cast<std::size_t>(cols));
    if (whole) {
        long long lo = static_cast<long long>(std::ceil(mn));
        long long hi = static_cast<long long>(std::floor(mx));
        if (lo > hi) return formula::makeScalar(ErrorValue{ErrorType::Value});
        std::uniform_int_distribution<long long> dist(lo, hi);
        for (auto& el : out.elements) el = static_cast<double>(dist(rng));
    } else {
        std::uniform_real_distribution<double> dist(mn, mx);
        for (auto& el : out.elements) el = dist(rng);
    }
    return formula::makeArray(std::move(out));
}

// ---- TRANSPOSE(array) ----
formula::EvalArg fnTranspose(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    formula::ArrayValue out(arr.cols, arr.rows);
    for (std::size_t r = 0; r < arr.rows; ++r) {
        for (std::size_t c = 0; c < arr.cols; ++c) out.at(c, r) = arr.at(r, c);
    }
    return formula::makeArray(std::move(out));
}

// ---- VSTACK / HSTACK ----
formula::EvalArg fnVStack(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::vector<formula::ArrayValue> parts;
    std::size_t totalRows = 0, maxCols = 0;
    for (const auto& arg : a) {
        auto arr = formula::toArray(arg, ctx);
        totalRows += arr.rows;
        maxCols = std::max(maxCols, arr.cols);
        parts.push_back(std::move(arr));
    }
    formula::ArrayValue out(totalRows, maxCols, ErrorValue{ErrorType::NA});
    std::size_t curRow = 0;
    for (const auto& p : parts) {
        for (std::size_t r = 0; r < p.rows; ++r) {
            for (std::size_t c = 0; c < p.cols; ++c) out.at(curRow + r, c) = p.at(r, c);
        }
        curRow += p.rows;
    }
    return formula::makeArray(std::move(out));
}

formula::EvalArg fnHStack(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::vector<formula::ArrayValue> parts;
    std::size_t maxRows = 0, totalCols = 0;
    for (const auto& arg : a) {
        auto arr = formula::toArray(arg, ctx);
        maxRows = std::max(maxRows, arr.rows);
        totalCols += arr.cols;
        parts.push_back(std::move(arr));
    }
    formula::ArrayValue out(maxRows, totalCols, ErrorValue{ErrorType::NA});
    std::size_t curCol = 0;
    for (const auto& p : parts) {
        for (std::size_t r = 0; r < p.rows; ++r) {
            for (std::size_t c = 0; c < p.cols; ++c) out.at(r, curCol + c) = p.at(r, c);
        }
        curCol += p.cols;
    }
    return formula::makeArray(std::move(out));
}

// ---- TAKE(array, rows, [columns]) / DROP(array, rows, [columns]) ----
formula::EvalArg fnTake(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    std::size_t rStart = 0, rEnd = arr.rows, cStart = 0, cEnd = arr.cols;
    if (a.size() >= 2 && !a[1].isOmitted()) {
        auto rd = toNumber(a[1].scalar);
        if (!rd) return formula::makeScalar(ErrorValue{ErrorType::Value});
        long long n = static_cast<long long>(std::trunc(*rd));
        if (n == 0) return formula::makeScalar(ErrorValue{ErrorType::Calc});
        if (n > 0) rEnd = std::min(arr.rows, static_cast<std::size_t>(n));
        else { std::size_t k = std::min(arr.rows, static_cast<std::size_t>(-n)); rStart = arr.rows - k; }
    }
    if (a.size() >= 3 && !a[2].isOmitted()) {
        auto cd = toNumber(a[2].scalar);
        if (!cd) return formula::makeScalar(ErrorValue{ErrorType::Value});
        long long n = static_cast<long long>(std::trunc(*cd));
        if (n == 0) return formula::makeScalar(ErrorValue{ErrorType::Calc});
        if (n > 0) cEnd = std::min(arr.cols, static_cast<std::size_t>(n));
        else { std::size_t k = std::min(arr.cols, static_cast<std::size_t>(-n)); cStart = arr.cols - k; }
    }
    formula::ArrayValue out(rEnd - rStart, cEnd - cStart);
    for (std::size_t r = rStart; r < rEnd; ++r) {
        for (std::size_t c = cStart; c < cEnd; ++c) out.at(r - rStart, c - cStart) = arr.at(r, c);
    }
    return formula::makeArray(std::move(out));
}

formula::EvalArg fnDrop(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    std::size_t rStart = 0, rEnd = arr.rows, cStart = 0, cEnd = arr.cols;
    if (a.size() >= 2 && !a[1].isOmitted()) {
        auto rd = toNumber(a[1].scalar);
        if (!rd) return formula::makeScalar(ErrorValue{ErrorType::Value});
        long long n = static_cast<long long>(std::trunc(*rd));
        if (n > 0) rStart = std::min(arr.rows, static_cast<std::size_t>(n));
        else if (n < 0) rEnd = arr.rows - std::min(arr.rows, static_cast<std::size_t>(-n));
    }
    if (a.size() >= 3 && !a[2].isOmitted()) {
        auto cd = toNumber(a[2].scalar);
        if (!cd) return formula::makeScalar(ErrorValue{ErrorType::Value});
        long long n = static_cast<long long>(std::trunc(*cd));
        if (n > 0) cStart = std::min(arr.cols, static_cast<std::size_t>(n));
        else if (n < 0) cEnd = arr.cols - std::min(arr.cols, static_cast<std::size_t>(-n));
    }
    if (rStart >= rEnd || cStart >= cEnd) return formula::makeScalar(ErrorValue{ErrorType::Calc});
    formula::ArrayValue out(rEnd - rStart, cEnd - cStart);
    for (std::size_t r = rStart; r < rEnd; ++r) {
        for (std::size_t c = cStart; c < cEnd; ++c) out.at(r - rStart, c - cStart) = arr.at(r, c);
    }
    return formula::makeArray(std::move(out));
}

// ---- EXPAND(array, rows, [columns], [pad_with]) ----
formula::EvalArg fnExpand(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    std::size_t rows = arr.rows, cols = arr.cols;
    if (a.size() >= 2 && !a[1].isOmitted()) {
        auto rd = toNumber(a[1].scalar);
        if (!rd || static_cast<long long>(std::trunc(*rd)) < static_cast<long long>(arr.rows)) {
            return formula::makeScalar(ErrorValue{ErrorType::Value});
        }
        rows = static_cast<std::size_t>(std::trunc(*rd));
    }
    if (a.size() >= 3 && !a[2].isOmitted()) {
        auto cd = toNumber(a[2].scalar);
        if (!cd || static_cast<long long>(std::trunc(*cd)) < static_cast<long long>(arr.cols)) {
            return formula::makeScalar(ErrorValue{ErrorType::Value});
        }
        cols = static_cast<std::size_t>(std::trunc(*cd));
    }
    CellValue pad = (a.size() >= 4 && !a[3].isOmitted()) ? a[3].scalar : CellValue{ErrorValue{ErrorType::NA}};
    formula::ArrayValue out(rows, cols, pad);
    for (std::size_t r = 0; r < arr.rows; ++r) {
        for (std::size_t c = 0; c < arr.cols; ++c) out.at(r, c) = arr.at(r, c);
    }
    return formula::makeArray(std::move(out));
}

// ---- TOCOL / TOROW ----
formula::EvalArg fnToVector(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx, bool asCol) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    int ignore = (a.size() >= 2 && !a[1].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[1].scalar).value_or(0.0))) : 0;
    bool byCol = (a.size() >= 3 && !a[2].isOmitted()) ? toBool(a[2].scalar).value_or(false) : false;
    std::vector<CellValue> vals;
    auto pushIf = [&](const CellValue& v) {
        if ((ignore == 1 || ignore == 3) && isEmpty(v)) return;
        if ((ignore == 2 || ignore == 3) && isError(v)) return;
        vals.push_back(v);
    };
    if (!byCol) {
        for (std::size_t r = 0; r < arr.rows; ++r)
            for (std::size_t c = 0; c < arr.cols; ++c) pushIf(arr.at(r, c));
    } else {
        for (std::size_t c = 0; c < arr.cols; ++c)
            for (std::size_t r = 0; r < arr.rows; ++r) pushIf(arr.at(r, c));
    }
    if (vals.empty()) return formula::makeScalar(ErrorValue{ErrorType::Calc});
    std::size_t rows = asCol ? vals.size() : 1;
    std::size_t cols = asCol ? 1 : vals.size();
    return formula::makeArray(formula::ArrayValue(rows, cols, std::move(vals)));
}

// ---- WRAPROWS / WRAPCOLS ----
formula::EvalArg fnWrap(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx, bool byRows) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    if (arr.rows != 1 && arr.cols != 1) return formula::makeScalar(ErrorValue{ErrorType::Value});
    auto wd = toNumber(a[1].scalar);
    if (!wd) return formula::makeScalar(ErrorValue{ErrorType::Value});
    long long w = static_cast<long long>(std::trunc(*wd));
    if (w < 1) return formula::makeScalar(ErrorValue{ErrorType::Num});
    CellValue pad = (a.size() >= 3 && !a[2].isOmitted()) ? a[2].scalar : CellValue{ErrorValue{ErrorType::NA}};
    std::size_t n = arr.elements.size();
    std::size_t wrap = static_cast<std::size_t>(w);
    std::size_t groups = (n + wrap - 1) / wrap;
    if (byRows) {
        formula::ArrayValue out(groups, wrap, pad);
        for (std::size_t i = 0; i < n; ++i) out.at(i / wrap, i % wrap) = arr.elements[i];
        return formula::makeArray(std::move(out));
    }
    formula::ArrayValue out(wrap, groups, pad);
    for (std::size_t i = 0; i < n; ++i) out.at(i % wrap, i / wrap) = arr.elements[i];
    return formula::makeArray(std::move(out));
}

// ---- CHOOSECOLS / CHOOSEROWS ----
formula::EvalArg fnChooseAxis(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx, bool isRows) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    std::size_t limit = isRows ? arr.rows : arr.cols;
    std::vector<std::size_t> idxs;
    for (std::size_t i = 1; i < a.size(); ++i) {
        auto d = toNumber(a[i].scalar);
        if (!d) return formula::makeScalar(ErrorValue{ErrorType::Value});
        long long k = static_cast<long long>(std::trunc(*d));
        if (k == 0) return formula::makeScalar(ErrorValue{ErrorType::Value});
        long long pos = (k > 0) ? (k - 1) : (static_cast<long long>(limit) + k);
        if (pos < 0 || static_cast<std::size_t>(pos) >= limit) return formula::makeScalar(ErrorValue{ErrorType::Value});
        idxs.push_back(static_cast<std::size_t>(pos));
    }
    if (isRows) {
        formula::ArrayValue out(idxs.size(), arr.cols);
        for (std::size_t r = 0; r < idxs.size(); ++r)
            for (std::size_t c = 0; c < arr.cols; ++c) out.at(r, c) = arr.at(idxs[r], c);
        return formula::makeArray(std::move(out));
    }
    formula::ArrayValue out(arr.rows, idxs.size());
    for (std::size_t r = 0; r < arr.rows; ++r)
        for (std::size_t c = 0; c < idxs.size(); ++c) out.at(r, c) = arr.at(r, idxs[c]);
    return formula::makeArray(std::move(out));
}

// ---- TRIMRANGE(range, [trim_rows], [trim_cols]) ----
formula::EvalArg fnTrimRange(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    int trimRows = (a.size() >= 2 && !a[1].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[1].scalar).value_or(3.0))) : 3;
    int trimCols = (a.size() >= 3 && !a[2].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[2].scalar).value_or(3.0))) : 3;
    auto rowEmpty = [&](std::size_t r) {
        for (std::size_t c = 0; c < arr.cols; ++c) if (!isEmpty(arr.at(r, c))) return false;
        return true;
    };
    auto colEmpty = [&](std::size_t c, std::size_t r0, std::size_t r1) {
        for (std::size_t r = r0; r < r1; ++r) if (!isEmpty(arr.at(r, c))) return false;
        return true;
    };
    std::size_t r0 = 0, r1 = arr.rows, c0 = 0, c1 = arr.cols;
    if (trimRows == 1 || trimRows == 3) while (r0 < r1 && rowEmpty(r0)) ++r0;
    if (trimRows == 2 || trimRows == 3) while (r1 > r0 && rowEmpty(r1 - 1)) --r1;
    if (trimCols == 1 || trimCols == 3) while (c0 < c1 && colEmpty(c0, r0, r1)) ++c0;
    if (trimCols == 2 || trimCols == 3) while (c1 > c0 && colEmpty(c1 - 1, r0, r1)) --c1;
    if (r0 >= r1 || c0 >= c1) return formula::makeScalar(ErrorValue{ErrorType::Calc});
    formula::ArrayValue out(r1 - r0, c1 - c0);
    for (std::size_t r = r0; r < r1; ++r)
        for (std::size_t c = c0; c < c1; ++c) out.at(r - r0, c - c0) = arr.at(r, c);
    return formula::makeArray(std::move(out));
}

// ---- LAMBDA helpers: BYROW, BYCOL, MAP, REDUCE, SCAN, MAKEARRAY ----
formula::EvalArg fnByRow(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    formula::ArrayValue out(arr.rows, 1);
    for (std::size_t r = 0; r < arr.rows; ++r) {
        formula::ArrayValue row(1, arr.cols);
        for (std::size_t c = 0; c < arr.cols; ++c) row.at(0, c) = arr.at(r, c);
        auto res = formula::callLambda(a[1], {formula::makeArray(std::move(row))}, ctx);
        out.at(r, 0) = formula::collapseToScalar(res, ctx);
    }
    return formula::makeArray(std::move(out));
}

formula::EvalArg fnByCol(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    formula::ArrayValue out(1, arr.cols);
    for (std::size_t c = 0; c < arr.cols; ++c) {
        formula::ArrayValue col(arr.rows, 1);
        for (std::size_t r = 0; r < arr.rows; ++r) col.at(r, 0) = arr.at(r, c);
        auto res = formula::callLambda(a[1], {formula::makeArray(std::move(col))}, ctx);
        out.at(0, c) = formula::collapseToScalar(res, ctx);
    }
    return formula::makeArray(std::move(out));
}

formula::EvalArg fnMap(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    if (a.size() < 2) return formula::makeScalar(ErrorValue{ErrorType::Value});
    const formula::EvalArg& fn = a.back();
    std::vector<formula::ArrayValue> arrs;
    std::size_t rows = 1, cols = 1;
    for (std::size_t i = 0; i + 1 < a.size(); ++i) {
        auto av = formula::toArray(a[i], ctx);
        rows = std::max(rows, av.rows);
        cols = std::max(cols, av.cols);
        arrs.push_back(std::move(av));
    }
    for (auto& av : arrs) {
        if (av.rows != rows || av.cols != cols) av = formula::broadcastTo(av, rows, cols);
    }
    formula::ArrayValue out(rows, cols);
    std::vector<formula::EvalArg> callArgs(arrs.size());
    for (std::size_t idx = 0; idx < rows * cols; ++idx) {
        for (std::size_t k = 0; k < arrs.size(); ++k) callArgs[k] = formula::makeScalar(arrs[k].elements[idx]);
        auto res = formula::callLambda(fn, callArgs, ctx);
        out.elements[idx] = formula::collapseToScalar(res, ctx);
    }
    return formula::makeArray(std::move(out));
}

formula::EvalArg fnReduce(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::EvalArg acc = a[0].isOmitted() ? formula::makeScalar(EmptyValue{}) : a[0];
    formula::ArrayValue arr = formula::toArray(a[1], ctx);
    for (const auto& el : arr.elements) {
        acc = formula::callLambda(a[2], {acc, formula::makeScalar(el)}, ctx);
    }
    return acc;
}

formula::EvalArg fnScan(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::EvalArg acc = a[0].isOmitted() ? formula::makeScalar(EmptyValue{}) : a[0];
    formula::ArrayValue arr = formula::toArray(a[1], ctx);
    formula::ArrayValue out(arr.rows, arr.cols);
    for (std::size_t i = 0; i < arr.elements.size(); ++i) {
        acc = formula::callLambda(a[2], {acc, formula::makeScalar(arr.elements[i])}, ctx);
        out.elements[i] = formula::collapseToScalar(acc, ctx);
    }
    return formula::makeArray(std::move(out));
}

formula::EvalArg fnMakeArray(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto rd = toNumber(a[0].scalar), cd = toNumber(a[1].scalar);
    if (!rd || !cd) return formula::makeScalar(ErrorValue{ErrorType::Value});
    long long rows = static_cast<long long>(std::trunc(*rd));
    long long cols = static_cast<long long>(std::trunc(*cd));
    if (rows < 1 || cols < 1 || rows * cols > 100000) return formula::makeScalar(ErrorValue{ErrorType::Value});
    formula::ArrayValue out(static_cast<std::size_t>(rows), static_cast<std::size_t>(cols));
    for (std::size_t r = 0; r < out.rows; ++r) {
        for (std::size_t c = 0; c < out.cols; ++c) {
            auto res = formula::callLambda(a[2], {formula::makeScalar(static_cast<double>(r + 1)),
                                                  formula::makeScalar(static_cast<double>(c + 1))}, ctx);
            out.at(r, c) = formula::collapseToScalar(res, ctx);
        }
    }
    return formula::makeArray(std::move(out));
}

// ---- GROUPBY(row_fields, values, function, [field_headers], [total_depth], [sort_order], [filter_array]) ----
formula::EvalArg fnGroupBy(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue rf = formula::toArray(a[0], ctx);
    formula::ArrayValue vf = formula::toArray(a[1], ctx);
    if (rf.rows != vf.rows || rf.rows == 0) return formula::makeScalar(ErrorValue{ErrorType::Value});
    int headers = (a.size() >= 4 && !a[3].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[3].scalar).value_or(0.0))) : 0;
    int totalDepth = (a.size() >= 5 && !a[4].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[4].scalar).value_or(1.0))) : 1;
    std::size_t startRow = (headers == 2 || headers == 3) ? 1 : 0;
    bool showHeader = (headers == 3);

    std::vector<bool> keep(rf.rows, true);
    if (a.size() >= 7 && !a[6].isOmitted()) {
        auto flt = formula::flatten(a[6], ctx);
        for (std::size_t i = 0; i < std::min(rf.rows, flt.size()); ++i) {
            keep[i] = toBool(flt[i]).value_or(false);
        }
    }

    struct Group {
        std::vector<CellValue> keyCells;
        std::vector<std::vector<CellValue>> colVals;
    };
    std::map<std::string, Group> groups;
    std::vector<std::vector<CellValue>> allColVals(vf.cols);

    for (std::size_t r = startRow; r < rf.rows; ++r) {
        if (!keep[r]) continue;
        std::string keyStr;
        std::vector<CellValue> keyCells;
        for (std::size_t c = 0; c < rf.cols; ++c) {
            if (c > 0) keyStr.push_back('\x1f');
            keyStr += toDisplayString(rf.at(r, c));
            keyCells.push_back(rf.at(r, c));
        }
        auto& g = groups[keyStr];
        if (g.keyCells.empty()) {
            g.keyCells = std::move(keyCells);
            g.colVals.resize(vf.cols);
        }
        for (std::size_t c = 0; c < vf.cols; ++c) {
            g.colVals[c].push_back(vf.at(r, c));
            allColVals[c].push_back(vf.at(r, c));
        }
    }

    std::size_t outRows = groups.size() + (showHeader ? 1 : 0) + (totalDepth != 0 ? 1 : 0);
    std::size_t outCols = rf.cols + vf.cols;
    formula::ArrayValue out(outRows, outCols, EmptyValue{});
    std::size_t curRow = 0;
    if (showHeader) {
        for (std::size_t c = 0; c < rf.cols; ++c) out.at(0, c) = rf.at(0, c);
        for (std::size_t c = 0; c < vf.cols; ++c) out.at(0, rf.cols + c) = vf.at(0, c);
        ++curRow;
    }
    auto evalAgg = [&](const std::vector<CellValue>& col) -> CellValue {
        formula::ArrayValue av(col.size(), 1, col);
        return formula::collapseToScalar(formula::callLambda(a[2], {formula::makeArray(std::move(av))}, ctx), ctx);
    };
    for (const auto& [_, g] : groups) {
        for (std::size_t c = 0; c < rf.cols; ++c) out.at(curRow, c) = g.keyCells[c];
        for (std::size_t c = 0; c < vf.cols; ++c) out.at(curRow, rf.cols + c) = evalAgg(g.colVals[c]);
        ++curRow;
    }
    if (totalDepth != 0) {
        out.at(curRow, 0) = std::string("Total");
        for (std::size_t c = 0; c < vf.cols; ++c) out.at(curRow, rf.cols + c) = evalAgg(allColVals[c]);
    }
    return formula::makeArray(std::move(out));
}

// ---- PIVOTBY(row_fields, col_fields, values, function, ...) ----
formula::EvalArg fnPivotBy(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue rf = formula::toArray(a[0], ctx);
    formula::ArrayValue cf = formula::toArray(a[1], ctx);
    formula::ArrayValue vf = formula::toArray(a[2], ctx);
    if (rf.rows != cf.rows || rf.rows != vf.rows || rf.rows == 0) {
        return formula::makeScalar(ErrorValue{ErrorType::Value});
    }
    int headers = (a.size() >= 5 && !a[4].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[4].scalar).value_or(0.0))) : 0;
    std::size_t startRow = (headers == 2 || headers == 3) ? 1 : 0;

    std::map<std::string, CellValue> rowKeys, colKeys;
    std::map<std::pair<std::string, std::string>, std::vector<CellValue>> cells;
    for (std::size_t r = startRow; r < rf.rows; ++r) {
        std::string rk = toDisplayString(rf.at(r, 0));
        std::string ck = toDisplayString(cf.at(r, 0));
        rowKeys.try_emplace(rk, rf.at(r, 0));
        colKeys.try_emplace(ck, cf.at(r, 0));
        cells[{rk, ck}].push_back(vf.at(r, 0));
    }

    std::vector<std::string> rList, cList;
    for (const auto& [k, _] : rowKeys) rList.push_back(k);
    for (const auto& [k, _] : colKeys) cList.push_back(k);

    formula::ArrayValue out(rList.size() + 1, cList.size() + 1, EmptyValue{});
    for (std::size_t c = 0; c < cList.size(); ++c) out.at(0, c + 1) = colKeys[cList[c]];
    for (std::size_t r = 0; r < rList.size(); ++r) {
        out.at(r + 1, 0) = rowKeys[rList[r]];
        for (std::size_t c = 0; c < cList.size(); ++c) {
            auto it = cells.find({rList[r], cList[c]});
            if (it != cells.end() && !it->second.empty()) {
                formula::ArrayValue av(it->second.size(), 1, it->second);
                out.at(r + 1, c + 1) = formula::collapseToScalar(
                    formula::callLambda(a[3], {formula::makeArray(std::move(av))}, ctx), ctx);
            }
        }
    }
    return formula::makeArray(std::move(out));
}

} // namespace

void registerDynArrayExtFunctions(formula::FunctionRegistry& registry) {
    registry.registerArrayFunction("FILTER", 2, 3, fnFilter);
    registry.registerArrayFunction("SORTBY", 2, 255, fnSortBy);
    registry.registerArrayFunction("RANDARRAY", 0, 5, fnRandArray, true);
    registry.registerArrayFunction("TRANSPOSE", 1, 1, fnTranspose);
    registry.registerArrayFunction("VSTACK", 1, 255, fnVStack);
    registry.registerArrayFunction("HSTACK", 1, 255, fnHStack);
    registry.registerArrayFunction("TAKE", 2, 3, fnTake);
    registry.registerArrayFunction("DROP", 2, 3, fnDrop);
    registry.registerArrayFunction("EXPAND", 2, 4, fnExpand);
    registry.registerArrayFunction("TOCOL", 1, 3, [](const auto& a, const auto& c) { return fnToVector(a, c, true); });
    registry.registerArrayFunction("TOROW", 1, 3, [](const auto& a, const auto& c) { return fnToVector(a, c, false); });
    registry.registerArrayFunction("WRAPROWS", 2, 3, [](const auto& a, const auto& c) { return fnWrap(a, c, true); });
    registry.registerArrayFunction("WRAPCOLS", 2, 3, [](const auto& a, const auto& c) { return fnWrap(a, c, false); });
    registry.registerArrayFunction("CHOOSECOLS", 2, 255, [](const auto& a, const auto& c) { return fnChooseAxis(a, c, false); });
    registry.registerArrayFunction("CHOOSEROWS", 2, 255, [](const auto& a, const auto& c) { return fnChooseAxis(a, c, true); });
    registry.registerArrayFunction("TRIMRANGE", 1, 3, fnTrimRange);

    registry.registerArrayFunction("BYROW", 2, 2, fnByRow);
    registry.registerArrayFunction("BYCOL", 2, 2, fnByCol);
    registry.registerArrayFunction("MAP", 2, 255, fnMap);
    registry.registerArrayFunction("REDUCE", 3, 3, fnReduce);
    registry.registerArrayFunction("SCAN", 3, 3, fnScan);
    registry.registerArrayFunction("MAKEARRAY", 3, 3, fnMakeArray);

    registry.registerArrayFunction("GROUPBY", 3, 10, fnGroupBy);
    registry.registerArrayFunction("PIVOTBY", 4, 11, fnPivotBy);

    // Special forms evaluated directly in Evaluator (registered here so FunctionRegistry::contains/size/names include them)
    registry.registerFunction({"LAMBDA", 1, 255, [](const auto&, const auto&) -> CellValue {
        return ErrorValue{ErrorType::Calc};
    }});
    registry.registerFunction({"LET", 3, 255, [](const auto&, const auto&) -> CellValue {
        return ErrorValue{ErrorType::Value};
    }});
}

} // namespace sheetcore::func_dynarray
