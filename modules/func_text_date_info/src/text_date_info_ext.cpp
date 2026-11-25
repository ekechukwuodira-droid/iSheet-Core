#include "sheetcore/formula/array_utils.hpp"
#include "sheetcore/formula/functions.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace sheetcore::func_text_date_info {

namespace {

std::string toLowerAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string toUpperAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return out;
}

// ---- Byte-length / DBCS text functions (UTF-8 byte semantics) ----
CellValue fnLenB(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    return static_cast<double>(toDisplayString(a[0].scalar).size());
}

CellValue fnLeftB(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    std::string s = toDisplayString(a[0].scalar);
    long long n = (a.size() >= 2 && !a[1].isOmitted()) ? static_cast<long long>(std::trunc(toNumber(a[1].scalar).value_or(-1.0))) : 1;
    if (n < 0) return ErrorValue{ErrorType::Value};
    return s.substr(0, std::min<std::size_t>(s.size(), static_cast<std::size_t>(n)));
}

CellValue fnRightB(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    std::string s = toDisplayString(a[0].scalar);
    long long n = (a.size() >= 2 && !a[1].isOmitted()) ? static_cast<long long>(std::trunc(toNumber(a[1].scalar).value_or(-1.0))) : 1;
    if (n < 0) return ErrorValue{ErrorType::Value};
    std::size_t cnt = std::min<std::size_t>(s.size(), static_cast<std::size_t>(n));
    return s.substr(s.size() - cnt, cnt);
}

CellValue fnMidB(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    std::string s = toDisplayString(a[0].scalar);
    auto st = toNumber(a[1].scalar), nb = toNumber(a[2].scalar);
    if (!st || !nb) return ErrorValue{ErrorType::Value};
    long long start = static_cast<long long>(std::trunc(*st));
    long long len = static_cast<long long>(std::trunc(*nb));
    if (start < 1 || len < 0) return ErrorValue{ErrorType::Value};
    std::size_t idx = static_cast<std::size_t>(start - 1);
    if (idx >= s.size()) return std::string{};
    return s.substr(idx, static_cast<std::size_t>(len));
}

CellValue fnFindSearchB(const std::vector<formula::EvalArg>& a, const formula::EvalContext&, bool caseSensitive) {
    std::string needle = toDisplayString(a[0].scalar);
    std::string hay = toDisplayString(a[1].scalar);
    long long start = (a.size() >= 3 && !a[2].isOmitted()) ? static_cast<long long>(std::trunc(toNumber(a[2].scalar).value_or(0.0))) : 1;
    if (start < 1 || static_cast<std::size_t>(start - 1) > hay.size()) return ErrorValue{ErrorType::Value};
    if (!caseSensitive) { needle = toLowerAscii(needle); hay = toLowerAscii(hay); }
    auto pos = hay.find(needle, static_cast<std::size_t>(start - 1));
    if (pos == std::string::npos) return ErrorValue{ErrorType::Value};
    return static_cast<double>(pos + 1);
}

CellValue fnReplaceB(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    std::string oldTxt = toDisplayString(a[0].scalar);
    auto st = toNumber(a[1].scalar), nb = toNumber(a[2].scalar);
    std::string newTxt = toDisplayString(a[3].scalar);
    if (!st || !nb) return ErrorValue{ErrorType::Value};
    long long start = static_cast<long long>(std::trunc(*st));
    long long len = static_cast<long long>(std::trunc(*nb));
    if (start < 1 || len < 0) return ErrorValue{ErrorType::Value};
    std::size_t idx = std::min<std::size_t>(oldTxt.size(), static_cast<std::size_t>(start - 1));
    std::size_t cnt = std::min<std::size_t>(oldTxt.size() - idx, static_cast<std::size_t>(len));
    return oldTxt.substr(0, idx) + newTxt + oldTxt.substr(idx + cnt);
}

// Full-width <-> Half-width ASCII conversion for ASC / DBCS / JIS
std::string fullToHalfWidth(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        // UTF-8 U+3000 (ideographic space) -> ' '
        if (i + 2 < s.size() && c0 == 0xE3 && static_cast<unsigned char>(s[i + 1]) == 0x80 && static_cast<unsigned char>(s[i + 2]) == 0x80) {
            out.push_back(' ');
            i += 3;
            continue;
        }
        // UTF-8 U+FF01..U+FF5E -> ASCII 0x21..0x7E
        if (i + 2 < s.size() && c0 == 0xEF) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            uint32_t cp = ((c0 & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
            if (cp >= 0xFF01 && cp <= 0xFF5E) {
                out.push_back(static_cast<char>(cp - 0xFEE0));
                i += 3;
                continue;
            }
        }
        out.push_back(s[i++]);
    }
    return out;
}

std::string halfToFullWidth(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char ch : s) {
        if (ch == ' ') {
            out.push_back(static_cast<char>(0xE3));
            out.push_back(static_cast<char>(0x80));
            out.push_back(static_cast<char>(0x80));
        } else if (ch >= 0x21 && ch <= 0x7E) {
            uint32_t cp = ch + 0xFEE0;
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

// ---- BAHTTEXT(number) ----
std::string thaiIntWords(unsigned long long n) {
    static const char* kDigits[10] = {
        "", "\xE0\xB8\xAB\xE0\xB8\x99\xE0\xB8\xB6\xE0\xB9\x88\xE0\xB8\x87",
        "\xE0\xB8\xAA\xE0\xB8\xAD\xE0\xB8\x87", "\xE0\xB8\xAA\xE0\xB8\xB2\xE0\xB8\xA1",
        "\xE0\xB8\xAA\xE0\xB8\xB5\xE0\xB9\x88", "\xE0\xB8\xAB\xE0\xB9\x89\xE0\xB8\xB2",
        "\xE0\xB8\xAB\xE0\xB8\x81", "\xE0\xB9\x80\xE0\xB8\x88\xE0\xB9\x87\xE0\xB8\x94",
        "\xE0\xB9\x81\xE0\xB8\x9B\xE0\xB8\x94", "\xE0\xB9\x80\xE0\xB8\x81\xE0\xB9\x89\xE0\xB8\xB2"
    };
    static const char* kPlaces[6] = {
        "", "\xE0\xB8\xAA\xE0\xB8\xB4\xE0\xB8\x9A",
        "\xE0\xB8\xA3\xE0\xB9\x89\xE0\xB8\xAD\xE0\xB8\xA2", "\xE0\xB8\x9E\xE0\xB8\xB1\xE0\xB8\x99",
        "\xE0\xB8\xAB\xE0\xB8\xA1\xE0\xB8\xB7\xE0\xB9\x88\xE0\xB8\x99", "\xE0\xB9\x81\xE0\xB8\xAA\xE0\xB8\x99"
    };
    if (n == 0) return "\xE0\xB8\xA8\xE0\xB8\xB9\xE0\xB8\x99\xE0\xB8\xA2\xE0\xB9\x8C"; // ศูนย์
    if (n >= 1000000ULL) {
        std::string hi = thaiIntWords(n / 1000000ULL) + "\xE0\xB8\xA5\xE0\xB9\x89\xE0\xB8\xB2\xE0\xB8\x99"; // ล้าน
        unsigned long long rem = n % 1000000ULL;
        return rem > 0 ? (hi + thaiIntWords(rem)) : hi;
    }
    std::string out;
    int digs[6] = {};
    int len = 0;
    unsigned long long tmp = n;
    while (tmp > 0 && len < 6) { digs[len++] = static_cast<int>(tmp % 10); tmp /= 10; }
    for (int i = len - 1; i >= 0; --i) {
        int d = digs[i];
        if (d == 0) continue;
        if (i == 0 && d == 1 && len > 1) {
            out += "\xE0\xB9\x80\xE0\xB8\xAD\xE0\xB9\x87\xE0\xB8\x94"; // เอ็ด
        } else if (i == 1 && d == 2) {
            out += "\xE0\xB8\xA2\xE0\xB8\xB5\xE0\xB9\x88\xE0\xB8\xAA\xE0\xB8\xB4\xE0\xB8\x9A"; // ยี่สิบ
        } else if (i == 1 && d == 1) {
            out += kPlaces[1]; // สิบ
        } else {
            out += kDigits[d];
            out += kPlaces[i];
        }
    }
    return out;
}

CellValue fnBahtText(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto n = toNumber(a[0].scalar);
    if (!n) return ErrorValue{ErrorType::Value};
    bool neg = *n < 0.0;
    unsigned long long satangTotal = static_cast<unsigned long long>(std::llround(std::fabs(*n) * 100.0));
    unsigned long long baht = satangTotal / 100ULL;
    unsigned long long satang = satangTotal % 100ULL;
    std::string out = neg ? "\xE0\xB8\xA5\xE0\xB8\x9A" : ""; // ลบ
    if (baht == 0 && satang == 0) {
        return out + "\xE0\xB8\xA8\xE0\xB8\xB9\xE0\xB8\x99\xE0\xB8\xA2\xE0\xB9\x8C\xE0\xB8\x9A\xE0\xB8\xB2\xE0\xB8\x97\xE0\xB8\x96\xE0\xB9\x89\xE0\xB8\xA7\xE0\xB8\x99";
    }
    if (baht > 0) {
        out += thaiIntWords(baht) + "\xE0\xB8\x9A\xE0\xB8\xB2\xE0\xB8\x97"; // บาท
    }
    if (satang == 0) {
        out += "\xE0\xB8\x96\xE0\xB9\x89\xE0\xB8\xA7\xE0\xB8\x99"; // ถ้วน
    } else {
        out += thaiIntWords(satang) + "\xE0\xB8\xAA\xE0\xB8\x95\xE0\xB8\xB2\xE0\xB8\x87\xE0\xB8\x84\xE0\xB9\x8C"; // สตางค์
    }
    return out;
}

// ---- ARRAYTOTEXT(array, [format]) ----
CellValue fnArrayToText(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue arr = formula::toArray(a[0], ctx);
    int fmt = (a.size() >= 2 && !a[1].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[1].scalar).value_or(0.0))) : 0;
    if (fmt != 0 && fmt != 1) return ErrorValue{ErrorType::Value};
    auto fmtCell = [fmt](const CellValue& v) -> std::string {
        if (fmt == 1 && isText(v)) return "\"" + std::get<std::string>(v) + "\"";
        return toDisplayString(v);
    };
    std::string out;
    if (fmt == 1) out.push_back('{');
    for (std::size_t r = 0; r < arr.rows; ++r) {
        if (r > 0) out += (fmt == 1 ? ";" : ", ");
        for (std::size_t c = 0; c < arr.cols; ++c) {
            if (c > 0) out += (fmt == 1 ? "," : ", ");
            out += fmtCell(arr.at(r, c));
        }
    }
    if (fmt == 1) out.push_back('}');
    return out;
}

// ---- REGEXTEST(text, pattern, [case_sensitivity]) ----
CellValue fnRegexTest(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    std::string text = toDisplayString(a[0].scalar);
    std::string pat = toDisplayString(a[1].scalar);
    int cs = (a.size() >= 3 && !a[2].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[2].scalar).value_or(0.0))) : 0;
    try {
        auto flags = std::regex::ECMAScript;
        if (cs != 0) flags |= std::regex::icase;
        std::regex re(pat, flags);
        return std::regex_search(text, re);
    } catch (...) {
        return ErrorValue{ErrorType::Value};
    }
}

// ---- TEXTSPLIT(text, col_delimiter, [row_delimiter], [ignore_empty], [match_mode], [pad_with]) ----
std::vector<std::string> splitByDelims(const std::string& s, const std::vector<std::string>& delims,
                                       bool ignoreEmpty, bool caseInsensitive) {
    if (delims.empty()) return {s};
    std::string hay = caseInsensitive ? toLowerAscii(s) : s;
    std::vector<std::string> needles = delims;
    if (caseInsensitive) for (auto& d : needles) d = toLowerAscii(d);

    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (pos <= s.size()) {
        std::size_t bestPos = std::string::npos;
        std::size_t bestLen = 0;
        for (const auto& d : needles) {
            if (d.empty()) continue;
            auto p = hay.find(d, pos);
            if (p != std::string::npos && (p < bestPos || (p == bestPos && d.size() > bestLen))) {
                bestPos = p;
                bestLen = d.size();
            }
        }
        if (bestPos == std::string::npos) {
            std::string tail = s.substr(pos);
            if (!ignoreEmpty || !tail.empty()) parts.push_back(std::move(tail));
            break;
        }
        std::string tok = s.substr(pos, bestPos - pos);
        if (!ignoreEmpty || !tok.empty()) parts.push_back(std::move(tok));
        pos = bestPos + bestLen;
    }
    return parts;
}

formula::EvalArg fnTextSplit(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::string text = toDisplayString(a[0].scalar);
    std::vector<std::string> colDelims, rowDelims;
    if (a.size() >= 2 && !a[1].isOmitted()) {
        for (const auto& v : formula::flatten(a[1], ctx)) colDelims.push_back(toDisplayString(v));
    }
    if (a.size() >= 3 && !a[2].isOmitted()) {
        for (const auto& v : formula::flatten(a[2], ctx)) rowDelims.push_back(toDisplayString(v));
    }
    if (colDelims.empty() && rowDelims.empty()) return formula::makeScalar(ErrorValue{ErrorType::Value});
    bool ignoreEmpty = (a.size() >= 4 && !a[3].isOmitted()) ? toBool(a[3].scalar).value_or(false) : false;
    bool caseInsens = (a.size() >= 5 && !a[4].isOmitted()) ? (toNumber(a[4].scalar).value_or(0.0) != 0.0) : false;
    CellValue pad = (a.size() >= 6 && !a[5].isOmitted()) ? a[5].scalar : CellValue{ErrorValue{ErrorType::NA}};

    auto rows = rowDelims.empty() ? std::vector<std::string>{text}
                                  : splitByDelims(text, rowDelims, ignoreEmpty, caseInsens);
    std::vector<std::vector<std::string>> grid;
    std::size_t maxCols = 0;
    for (const auto& r : rows) {
        auto cols = colDelims.empty() ? std::vector<std::string>{r}
                                      : splitByDelims(r, colDelims, ignoreEmpty, caseInsens);
        maxCols = std::max(maxCols, cols.size());
        grid.push_back(std::move(cols));
    }
    if (grid.empty() || maxCols == 0) return formula::makeScalar(ErrorValue{ErrorType::Calc});
    formula::ArrayValue out(grid.size(), maxCols, pad);
    for (std::size_t r = 0; r < grid.size(); ++r) {
        for (std::size_t c = 0; c < grid[r].size(); ++c) out.at(r, c) = grid[r][c];
    }
    return formula::makeArray(std::move(out));
}

// ---- NETWORKDAYS.INTL / WORKDAY.INTL ----
// Weekend mask: index 0=Mon .. 6=Sun (true = weekend)
bool parseWeekendMask(const formula::EvalArg& arg, std::array<bool, 7>& mask) {
    mask.fill(false);
    if (arg.isOmitted() || isEmpty(arg.scalar)) {
        mask[5] = mask[6] = true; // Sat/Sun
        return true;
    }
    if (isText(arg.scalar)) {
        const std::string& s = std::get<std::string>(arg.scalar);
        if (s.size() != 7) return false;
        int workdays = 0;
        for (std::size_t i = 0; i < 7; ++i) {
            if (s[i] == '1') mask[i] = true;
            else if (s[i] == '0') { mask[i] = false; ++workdays; }
            else return false;
        }
        return workdays > 0;
    }
    auto codeD = toNumber(arg.scalar);
    if (!codeD) return false;
    int code = static_cast<int>(std::trunc(*codeD));
    switch (code) {
        case 1: mask[5] = mask[6] = true; return true; // Sat, Sun
        case 2: mask[6] = mask[0] = true; return true; // Sun, Mon
        case 3: mask[0] = mask[1] = true; return true; // Mon, Tue
        case 4: mask[1] = mask[2] = true; return true; // Tue, Wed
        case 5: mask[2] = mask[3] = true; return true; // Wed, Thu
        case 6: mask[3] = mask[4] = true; return true; // Thu, Fri
        case 7: mask[4] = mask[5] = true; return true; // Fri, Sat
        case 11: mask[6] = true; return true; // Sun only
        case 12: mask[0] = true; return true; // Mon only
        case 13: mask[1] = true; return true; // Tue only
        case 14: mask[2] = true; return true; // Wed only
        case 15: mask[3] = true; return true; // Thu only
        case 16: mask[4] = true; return true; // Fri only
        case 17: mask[5] = true; return true; // Sat only
        default: return false;
    }
}

// Excel serial -> 0=Mon .. 6=Sun (serial 1 = 1899-12-31 = Sunday = 6)
int monBasedWeekday(long long serial) {
    return static_cast<int>(((serial - 2) % 7 + 7) % 7);
}

CellValue fnNetworkDaysIntl(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto s1 = toNumber(a[0].scalar), s2 = toNumber(a[1].scalar);
    if (!s1 || !s2) return ErrorValue{ErrorType::Value};
    std::array<bool, 7> wk{};
    if (a.size() >= 3) {
        if (!parseWeekendMask(a[2], wk)) return ErrorValue{ErrorType::Num};
    } else {
        wk[5] = wk[6] = true;
    }
    std::unordered_set<long long> holidays;
    if (a.size() >= 4 && !a[3].isOmitted()) {
        for (const auto& v : formula::flatten(a[3], ctx)) {
            if (auto d = toNumber(v)) holidays.insert(static_cast<long long>(std::floor(*d)));
        }
    }
    long long d1 = static_cast<long long>(std::floor(*s1));
    long long d2 = static_cast<long long>(std::floor(*s2));
    int sign = d1 <= d2 ? 1 : -1;
    if (d1 > d2) std::swap(d1, d2);
    long long count = 0;
    for (long long d = d1; d <= d2; ++d) {
        if (!wk[static_cast<std::size_t>(monBasedWeekday(d))] && !holidays.contains(d)) ++count;
    }
    return static_cast<double>(sign * count);
}

CellValue fnWorkdayIntl(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto startD = toNumber(a[0].scalar), daysD = toNumber(a[1].scalar);
    if (!startD || !daysD) return ErrorValue{ErrorType::Value};
    std::array<bool, 7> wk{};
    if (a.size() >= 3) {
        if (!parseWeekendMask(a[2], wk)) return ErrorValue{ErrorType::Num};
    } else {
        wk[5] = wk[6] = true;
    }
    std::unordered_set<long long> holidays;
    if (a.size() >= 4 && !a[3].isOmitted()) {
        for (const auto& v : formula::flatten(a[3], ctx)) {
            if (auto d = toNumber(v)) holidays.insert(static_cast<long long>(std::floor(*d)));
        }
    }
    long long cur = static_cast<long long>(std::floor(*startD));
    long long remaining = static_cast<long long>(std::trunc(*daysD));
    int step = remaining >= 0 ? 1 : -1;
    remaining = std::llabs(remaining);
    while (remaining > 0) {
        cur += step;
        if (!wk[static_cast<std::size_t>(monBasedWeekday(cur))] && !holidays.contains(cur)) {
            --remaining;
        }
    }
    return static_cast<double>(cur);
}

// ---- Information & Formula introspection: ISOMITTED, ISREF, ISFORMULA, FORMULATEXT, CELL, INFO ----
CellValue fnIsOmitted(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    return a[0].isOmitted();
}

CellValue fnIsRef(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    return a[0].isRange();
}

CellValue fnIsFormula(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    if (!a[0].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
    return ctx.values->getCellFormula(a[0].rangeRef->sheetId, a[0].rangeRef->range.normalized().start).has_value();
}

CellValue fnFormulaText(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    if (!a[0].isRange() || !ctx.values) return ErrorValue{ErrorType::NA};
    auto f = ctx.values->getCellFormula(a[0].rangeRef->sheetId, a[0].rangeRef->range.normalized().start);
    return f ? CellValue{*f} : CellValue{ErrorValue{ErrorType::NA}};
}

CellValue fnCell(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::string info = toLowerAscii(toDisplayString(a[0].scalar));
    SheetId sid = ctx.activeSheetId;
    CellCoord coord = ctx.currentCell;
    CellValue val = EmptyValue{};
    if (a.size() >= 2 && a[1].isRange()) {
        sid = a[1].rangeRef->sheetId;
        coord = a[1].rangeRef->range.normalized().start;
        val = ctx.values ? ctx.values->getCellValue(sid, coord) : a[1].scalar;
    } else if (ctx.values) {
        val = ctx.values->getCellValue(sid, coord);
    }
    if (info == "address") return coord.toA1(true, true);
    if (info == "col") return static_cast<double>(coord.col + 1);
    if (info == "row") return static_cast<double>(coord.row + 1);
    if (info == "contents") return val;
    if (info == "type") {
        if (isEmpty(val)) return std::string("b");
        if (isText(val)) return std::string("l");
        return std::string("v");
    }
    if (info == "filename") return ctx.values ? ctx.values->sheetName(sid).value_or("") : std::string{};
    if (info == "format") return std::string("G");
    if (info == "color" || info == "parentheses" || info == "protect") return 0.0;
    if (info == "prefix") return isText(val) ? std::string("'") : std::string{};
    if (info == "width") return 8.0;
    return ErrorValue{ErrorType::Value};
}

CellValue fnInfo(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::string t = toLowerAscii(toDisplayString(a[0].scalar));
    if (t == "numfile") return static_cast<double>(ctx.values ? std::max<std::size_t>(1, ctx.values->sheetNames().size()) : 1);
    if (t == "recalc") return std::string("Automatic");
    if (t == "release") return std::string("16.0");
    if (t == "system") return std::string("pcdos");
    if (t == "osversion") return std::string("Windows (64-bit) NT 10.00");
    if (t == "origin") return std::string("$A$1");
    if (t == "directory") return std::string(".");
    return ErrorValue{ErrorType::Value};
}

// ---- Reference & Lookup: AREAS, SHEET, SHEETS, HYPERLINK, INDIRECT, OFFSET, LOOKUP ----
CellValue fnAreas(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    return a[0].isRange() ? CellValue{1.0} : CellValue{ErrorValue{ErrorType::Value}};
}

CellValue fnSheet(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    if (!ctx.values) return 1.0;
    auto names = ctx.values->sheetNames();
    if (a.empty() || a[0].isOmitted()) {
        auto curName = ctx.values->sheetName(ctx.activeSheetId);
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (curName && names[i] == *curName) return static_cast<double>(i + 1);
        }
        return static_cast<double>(ctx.activeSheetId);
    }
    if (a[0].isRange()) {
        auto targetName = ctx.values->sheetName(a[0].rangeRef->sheetId);
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (targetName && names[i] == *targetName) return static_cast<double>(i + 1);
        }
        return static_cast<double>(a[0].rangeRef->sheetId);
    }
    std::string wanted = toLowerAscii(toDisplayString(a[0].scalar));
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (toLowerAscii(names[i]) == wanted) return static_cast<double>(i + 1);
    }
    return ErrorValue{ErrorType::NA};
}

CellValue fnSheets(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    if (!a.empty() && !a[0].isOmitted()) return 1.0;
    if (!ctx.values) return 1.0;
    return static_cast<double>(std::max<std::size_t>(1, ctx.values->sheetNames().size()));
}

CellValue fnHyperlink(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    return (a.size() >= 2 && !a[1].isOmitted()) ? a[1].scalar : a[0].scalar;
}

std::optional<CellCoord> parseR1C1Coord(std::string_view s, CellCoord base) {
    std::string u = toUpperAscii(s);
    if (u.empty() || u[0] != 'R') return std::nullopt;
    auto cPos = u.find('C', 1);
    if (cPos == std::string::npos) return std::nullopt;
    auto parsePart = [](std::string_view part, long long cur) -> std::optional<long long> {
        if (part.empty()) return cur;
        if (part.front() == '[' && part.back() == ']') {
            try { return cur + std::stoll(std::string(part.substr(1, part.size() - 2))); }
            catch (...) { return std::nullopt; }
        }
        try { return std::stoll(std::string(part)) - 1; }
        catch (...) { return std::nullopt; }
    };
    auto r = parsePart(std::string_view(u).substr(1, cPos - 1), static_cast<long long>(base.row));
    auto c = parsePart(std::string_view(u).substr(cPos + 1), static_cast<long long>(base.col));
    if (!r || !c || *r < 0 || *c < 0) return std::nullopt;
    return CellCoord{static_cast<RowIndex>(*r), static_cast<ColIndex>(*c)};
}

formula::EvalArg fnIndirect(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    if (!ctx.values) return formula::makeScalar(ErrorValue{ErrorType::Ref});
    std::string refText = toDisplayString(a[0].scalar);
    bool a1Style = (a.size() >= 2 && !a[1].isOmitted()) ? toBool(a[1].scalar).value_or(true) : true;

    SheetId sid = ctx.activeSheetId;
    auto bang = refText.find('!');
    std::string addrPart = refText;
    if (bang != std::string::npos) {
        std::string sh = refText.substr(0, bang);
        if (sh.size() >= 2 && sh.front() == '\'' && sh.back() == '\'') sh = sh.substr(1, sh.size() - 2);
        auto resolved = ctx.values->resolveSheetId(sh);
        if (!resolved) return formula::makeScalar(ErrorValue{ErrorType::Ref});
        sid = *resolved;
        addrPart = refText.substr(bang + 1);
    }

    auto colon = addrPart.find(':');
    auto parseCell = [&](std::string_view tok) -> std::optional<CellCoord> {
        return a1Style ? CellCoord::fromA1(tok) : parseR1C1Coord(tok, ctx.currentCell);
    };
    if (colon == std::string::npos) {
        auto c = parseCell(addrPart);
        if (!c) return formula::makeScalar(ErrorValue{ErrorType::Ref});
        return formula::makeRef(SheetRangeAddress{sid, RangeAddress{*c, *c}}, ctx);
    }
    auto c1 = parseCell(std::string_view(addrPart).substr(0, colon));
    auto c2 = parseCell(std::string_view(addrPart).substr(colon + 1));
    if (!c1 || !c2) return formula::makeScalar(ErrorValue{ErrorType::Ref});
    return formula::makeRef(SheetRangeAddress{sid, RangeAddress{*c1, *c2}.normalized()}, ctx);
}

formula::EvalArg fnOffset(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    if (!a[0].isRange() || !ctx.values) return formula::makeScalar(ErrorValue{ErrorType::Value});
    auto rOffD = toNumber(a[1].scalar), cOffD = toNumber(a[2].scalar);
    if (!rOffD || !cOffD) return formula::makeScalar(ErrorValue{ErrorType::Value});
    RangeAddress base = a[0].rangeRef->range.normalized();
    long long r0 = static_cast<long long>(base.start.row) + static_cast<long long>(std::trunc(*rOffD));
    long long c0 = static_cast<long long>(base.start.col) + static_cast<long long>(std::trunc(*cOffD));
    long long h = static_cast<long long>(base.rowCount());
    long long w = static_cast<long long>(base.colCount());
    if (a.size() >= 4 && !a[3].isOmitted()) {
        auto hd = toNumber(a[3].scalar);
        if (!hd) return formula::makeScalar(ErrorValue{ErrorType::Value});
        h = static_cast<long long>(std::trunc(*hd));
    }
    if (a.size() >= 5 && !a[4].isOmitted()) {
        auto wd = toNumber(a[4].scalar);
        if (!wd) return formula::makeScalar(ErrorValue{ErrorType::Value});
        w = static_cast<long long>(std::trunc(*wd));
    }
    if (h <= 0 || w <= 0 || r0 < 0 || c0 < 0) return formula::makeScalar(ErrorValue{ErrorType::Ref});
    RangeAddress shifted{
        CellCoord{static_cast<RowIndex>(r0), static_cast<ColIndex>(c0)},
        CellCoord{static_cast<RowIndex>(r0 + h - 1), static_cast<ColIndex>(c0 + w - 1)}
    };
    return formula::makeRef(SheetRangeAddress{a[0].rangeRef->sheetId, shifted}, ctx);
}

// ---- LOOKUP(lookup_value, lookup_vector, [result_vector]) ----
CellValue fnLookup(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    const CellValue& key = a[0].scalar;
    auto cmpVal = [](const CellValue& x, const CellValue& y) -> int {
        if (isNumber(x) && isNumber(y)) {
            double dx = std::get<double>(x), dy = std::get<double>(y);
            return (dx < dy) ? -1 : ((dx > dy) ? 1 : 0);
        }
        std::string sx = toLowerAscii(toDisplayString(x)), sy = toLowerAscii(toDisplayString(y));
        return (sx < sy) ? -1 : ((sx > sy) ? 1 : 0);
    };

    std::vector<CellValue> searchVec, resultVec;
    if (a.size() >= 3 && !a[2].isOmitted()) {
        searchVec = formula::flatten(a[1], ctx);
        resultVec = formula::flatten(a[2], ctx);
    } else {
        formula::ArrayValue arr = formula::toArray(a[1], ctx);
        if (arr.rows >= arr.cols) {
            for (std::size_t r = 0; r < arr.rows; ++r) {
                searchVec.push_back(arr.at(r, 0));
                resultVec.push_back(arr.at(r, arr.cols - 1));
            }
        } else {
            for (std::size_t c = 0; c < arr.cols; ++c) {
                searchVec.push_back(arr.at(0, c));
                resultVec.push_back(arr.at(arr.rows - 1, c));
            }
        }
    }
    int bestIdx = -1;
    for (std::size_t i = 0; i < searchVec.size(); ++i) {
        if (cmpVal(searchVec[i], key) <= 0) bestIdx = static_cast<int>(i);
        else break;
    }
    if (bestIdx < 0 || static_cast<std::size_t>(bestIdx) >= resultVec.size()) return ErrorValue{ErrorType::NA};
    return resultVec[static_cast<std::size_t>(bestIdx)];
}

} // namespace

void registerTextDateInfoExtFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"LENB", 1, 1, fnLenB});
    registry.registerFunction({"LEFTB", 1, 2, fnLeftB});
    registry.registerFunction({"RIGHTB", 1, 2, fnRightB});
    registry.registerFunction({"MIDB", 3, 3, fnMidB});
    registry.registerFunction({"FINDB", 2, 3, [](const auto& a, const auto& c) { return fnFindSearchB(a, c, true); }});
    registry.registerFunction({"SEARCHB", 2, 3, [](const auto& a, const auto& c) { return fnFindSearchB(a, c, false); }});
    registry.registerFunction({"REPLACEB", 4, 4, fnReplaceB});
    registry.registerFunction({"ASC", 1, 1, [](const auto& a, const auto&) -> CellValue {
        return fullToHalfWidth(toDisplayString(a[0].scalar));
    }});
    registry.registerFunction({"DBCS", 1, 1, [](const auto& a, const auto&) -> CellValue {
        return halfToFullWidth(toDisplayString(a[0].scalar));
    }});
    registry.registerFunction({"JIS", 1, 1, [](const auto& a, const auto&) -> CellValue {
        return halfToFullWidth(toDisplayString(a[0].scalar));
    }});
    registry.registerFunction({"PHONETIC", 1, 1, [](const auto& a, const auto&) -> CellValue {
        return isText(a[0].scalar) ? a[0].scalar : CellValue{std::string{}};
    }});
    registry.registerFunction({"BAHTTEXT", 1, 1, fnBahtText});

    registry.registerFunction({"ARRAYTOTEXT", 1, 2, fnArrayToText});
    registry.registerFunction({"REGEXTEST", 2, 3, fnRegexTest});
    registry.registerArrayFunction("TEXTSPLIT", 2, 6, fnTextSplit);

    registry.registerFunction({"NETWORKDAYS.INTL", 2, 4, fnNetworkDaysIntl});
    registry.registerFunction({"WORKDAY.INTL", 2, 4, fnWorkdayIntl});

    registry.registerFunction({"ISOMITTED", 1, 1, fnIsOmitted});
    registry.registerFunction({"ISREF", 1, 1, fnIsRef});
    registry.registerFunction({"ISFORMULA", 1, 1, fnIsFormula});
    registry.registerFunction({"FORMULATEXT", 1, 1, fnFormulaText});
    registry.registerFunction({"CELL", 1, 2, fnCell});
    registry.registerFunction({"INFO", 1, 1, fnInfo});

    registry.registerFunction({"AREAS", 1, 1, fnAreas});
    registry.registerFunction({"SHEET", 0, 1, fnSheet});
    registry.registerFunction({"SHEETS", 0, 1, fnSheets});
    registry.registerFunction({"HYPERLINK", 1, 2, fnHyperlink});
    registry.registerArrayFunction("INDIRECT", 1, 2, fnIndirect, true);
    registry.registerArrayFunction("OFFSET", 3, 5, fnOffset, true);
    registry.registerFunction({"LOOKUP", 2, 3, fnLookup});
}

} // namespace sheetcore::func_text_date_info
