#include "sheetcore/func_text_date_info/register.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <regex>
#include <sstream>

namespace sheetcore::func_text_date_info {

namespace {

double civilToSerial(long long y, int m, int d) {
    y -= (m <= 2);
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153u * static_cast<unsigned>(m > 2 ? m - 3 : m + 9) + 2u) / 5u + static_cast<unsigned>(d) - 1u;
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

std::string formatFixed(double val, int decimals, bool noCommas) {
    bool neg = val < 0.0;
    double absVal = std::fabs(val);
    int d = std::max(0, decimals);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(d) << absVal;
    std::string raw = oss.str();
    if (!noCommas) {
        auto dot = raw.find('.');
        std::string ip = dot == std::string::npos ? raw : raw.substr(0, dot);
        std::string fp = dot == std::string::npos ? "" : raw.substr(dot);
        std::string grouped;
        int cnt = 0;
        for (auto it = ip.rbegin(); it != ip.rend(); ++it) {
            if (cnt > 0 && cnt % 3 == 0) grouped.push_back(',');
            grouped.push_back(*it);
            ++cnt;
        }
        std::reverse(grouped.begin(), grouped.end());
        raw = grouped + fp;
    }
    return neg ? ("-" + raw) : raw;
}

} // namespace

void registerTextDateInfoExtFunctions(formula::FunctionRegistry& registry);
void registerExternalFunctions(formula::FunctionRegistry& registry);

void registerTextDateInfoFunctions(formula::FunctionRegistry& registry) {
    // ---- Extended Text & Regex (20) ----
    registry.registerFunction({"EXACT", 2, 2, [](const auto& a, const auto&) -> CellValue {
        return toDisplayString(a[0].scalar) == toDisplayString(a[1].scalar);
    }});
    registry.registerFunction({"CHAR", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto n = toNumber(a[0].scalar);
        if (!n || *n < 1.0 || *n > 255.0) return ErrorValue{ErrorType::Value};
        return std::string(1, static_cast<char>(static_cast<unsigned char>(*n)));
    }});
    registry.registerFunction({"CODE", 1, 1, [](const auto& a, const auto&) -> CellValue {
        std::string s = toDisplayString(a[0].scalar);
        if (s.empty()) return ErrorValue{ErrorType::Value};
        return static_cast<double>(static_cast<unsigned char>(s[0]));
    }});
    registry.registerFunction({"UNICHAR", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto n = toNumber(a[0].scalar);
        if (!n || *n < 1.0 || *n > 1114111.0) return ErrorValue{ErrorType::Value};
        std::uint32_t cp = static_cast<std::uint32_t>(std::trunc(*n));
        std::string out;
        if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return out;
    }});
    registry.registerFunction({"UNICODE", 1, 1, [](const auto& a, const auto&) -> CellValue {
        std::string s = toDisplayString(a[0].scalar);
        if (s.empty()) return ErrorValue{ErrorType::Value};
        unsigned char c0 = static_cast<unsigned char>(s[0]);
        if (c0 < 0x80) return static_cast<double>(c0);
        if ((c0 & 0xE0) == 0xC0 && s.size() >= 2) {
            return static_cast<double>(((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[1]) & 0x3F));
        }
        if ((c0 & 0xF0) == 0xE0 && s.size() >= 3) {
            return static_cast<double>(((c0 & 0x0F) << 12) |
                ((static_cast<unsigned char>(s[1]) & 0x3F) << 6) |
                (static_cast<unsigned char>(s[2]) & 0x3F));
        }
        return static_cast<double>(c0);
    }});
    registry.registerFunction({"CLEAN", 1, 1, [](const auto& a, const auto&) -> CellValue {
        std::string s = toDisplayString(a[0].scalar);
        std::string out;
        for (char c : s) {
            if (static_cast<unsigned char>(c) >= 32) out.push_back(c);
        }
        return out;
    }});
    registry.registerFunction({"FIXED", 1, 3, [](const auto& a, const auto&) -> CellValue {
        auto n = toNumber(a[0].scalar);
        if (!n) return ErrorValue{ErrorType::Value};
        int dec = a.size() >= 2 ? static_cast<int>(toNumber(a[1].scalar).value_or(2.0)) : 2;
        bool noCommas = a.size() >= 3 ? toBool(a[2].scalar).value_or(false) : false;
        return formatFixed(*n, dec, noCommas);
    }});
    registry.registerFunction({"DOLLAR", 1, 2, [](const auto& a, const auto&) -> CellValue {
        auto n = toNumber(a[0].scalar);
        if (!n) return ErrorValue{ErrorType::Value};
        int dec = a.size() >= 2 ? static_cast<int>(toNumber(a[1].scalar).value_or(2.0)) : 2;
        if (*n < 0.0) return "-$" + formatFixed(std::fabs(*n), dec, false);
        return "$" + formatFixed(*n, dec, false);
    }});
    registry.registerFunction({"NUMBERVALUE", 1, 3, [](const auto& a, const auto&) -> CellValue {
        std::string s = toDisplayString(a[0].scalar);
        char decSep = (a.size() >= 2 && !toDisplayString(a[1].scalar).empty()) ? toDisplayString(a[1].scalar)[0] : '.';
        char grpSep = (a.size() >= 3 && !toDisplayString(a[2].scalar).empty()) ? toDisplayString(a[2].scalar)[0] : ',';
        std::string cleaned;
        for (char c : s) {
            if (c == grpSep || std::isspace(static_cast<unsigned char>(c))) continue;
            cleaned.push_back(c == decSep ? '.' : c);
        }
        auto n = toNumber(cleaned);
        return n ? CellValue{*n} : CellValue{ErrorValue{ErrorType::Value}};
    }});
    registry.registerFunction({"T", 1, 1, [](const auto& a, const auto&) -> CellValue {
        return isText(a[0].scalar) ? a[0].scalar : CellValue{std::string{}};
    }});
    registry.registerFunction({"N", 1, 1, [](const auto& a, const auto&) -> CellValue {
        if (isError(a[0].scalar)) return a[0].scalar;
        if (isNumber(a[0].scalar)) return a[0].scalar;
        if (isBool(a[0].scalar)) return std::get<bool>(a[0].scalar) ? 1.0 : 0.0;
        if (isDateTime(a[0].scalar)) return std::get<DateTimeValue>(a[0].scalar).serial;
        return 0.0;
    }});
    registry.registerFunction({"TEXTBEFORE", 2, 3, [](const auto& a, const auto&) -> CellValue {
        std::string s = toDisplayString(a[0].scalar);
        std::string d = toDisplayString(a[1].scalar);
        auto pos = s.find(d);
        if (pos == std::string::npos) return ErrorValue{ErrorType::NA};
        return s.substr(0, pos);
    }});
    registry.registerFunction({"TEXTAFTER", 2, 3, [](const auto& a, const auto&) -> CellValue {
        std::string s = toDisplayString(a[0].scalar);
        std::string d = toDisplayString(a[1].scalar);
        auto pos = s.find(d);
        if (pos == std::string::npos) return ErrorValue{ErrorType::NA};
        return s.substr(pos + d.size());
    }});
    registry.registerFunction({"VALUETOTEXT", 1, 2, [](const auto& a, const auto&) -> CellValue {
        return toDisplayString(a[0].scalar);
    }});
    registry.registerFunction({"ENCODEURL", 1, 1, [](const auto& a, const auto&) -> CellValue {
        std::string s = toDisplayString(a[0].scalar);
        std::ostringstream oss;
        oss << std::uppercase << std::hex;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') oss << static_cast<char>(c);
            else oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
        return oss.str();
    }});
    registry.registerFunction({"CONCATENATE", 1, 255, [](const auto& args, const auto&) -> CellValue {
        std::string out;
        for (const auto& a : args) {
            if (isError(a.scalar)) return a.scalar;
            out += toDisplayString(a.scalar);
        }
        return out;
    }});
    registry.registerFunction({"REGEXMATCH", 2, 2, [](const auto& a, const auto&) -> CellValue {
        try {
            std::regex re(toDisplayString(a[1].scalar));
            return std::regex_search(toDisplayString(a[0].scalar), re);
        } catch (...) { return ErrorValue{ErrorType::Value}; }
    }});
    registry.registerFunction({"REGEXEXTRACT", 2, 2, [](const auto& a, const auto&) -> CellValue {
        try {
            std::string text = toDisplayString(a[0].scalar);
            std::regex re(toDisplayString(a[1].scalar));
            std::smatch m;
            if (!std::regex_search(text, m, re)) return ErrorValue{ErrorType::NA};
            return m.str(0);
        } catch (...) { return ErrorValue{ErrorType::Value}; }
    }});
    registry.registerFunction({"REGEXREPLACE", 3, 3, [](const auto& a, const auto&) -> CellValue {
        try {
            std::regex re(toDisplayString(a[1].scalar));
            return std::regex_replace(toDisplayString(a[0].scalar), re, toDisplayString(a[2].scalar));
        } catch (...) { return ErrorValue{ErrorType::Value}; }
    }});

    // ---- Extended Date & Time (8) ----
    registry.registerFunction({"TIME", 3, 3, [](const auto& a, const auto&) -> CellValue {
        auto h = toNumber(a[0].scalar), m = toNumber(a[1].scalar), s = toNumber(a[2].scalar);
        if (!h || !m || !s) return ErrorValue{ErrorType::Value};
        double total = (*h) * 3600.0 + (*m) * 60.0 + (*s);
        if (total < 0.0) return ErrorValue{ErrorType::Num};
        return DateTimeValue{std::fmod(total, 86400.0) / 86400.0};
    }});
    registry.registerFunction({"DATEVALUE", 1, 1, [](const auto& a, const auto&) -> CellValue {
        std::string s = toDisplayString(a[0].scalar);
        int y = 0, m = 0, d = 0;
        char sep1 = 0, sep2 = 0;
        std::istringstream iss(s);
        if ((iss >> y >> sep1 >> m >> sep2 >> d) && (sep1 == '-' || sep1 == '/') && sep1 == sep2) {
            return DateTimeValue{civilToSerial(y, m, d)};
        }
        return ErrorValue{ErrorType::Value};
    }});
    registry.registerFunction({"TIMEVALUE", 1, 1, [](const auto& a, const auto&) -> CellValue {
        std::string s = toDisplayString(a[0].scalar);
        int h = 0, m = 0, sec = 0;
        char c1 = 0, c2 = 0;
        std::istringstream iss(s);
        if (iss >> h >> c1 >> m && c1 == ':') {
            if (iss >> c2 >> sec && c2 == ':') {} else { sec = 0; }
            return DateTimeValue{(h * 3600.0 + m * 60.0 + sec) / 86400.0};
        }
        return ErrorValue{ErrorType::Value};
    }});
    registry.registerFunction({"DAYS", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto endD = toNumber(a[0].scalar), startD = toNumber(a[1].scalar);
        if (!endD || !startD) return ErrorValue{ErrorType::Value};
        return std::floor(*endD) - std::floor(*startD);
    }});
    registry.registerFunction({"DAYS360", 2, 3, [](const auto& a, const auto&) -> CellValue {
        auto s1 = toNumber(a[0].scalar), s2 = toNumber(a[1].scalar);
        if (!s1 || !s2) return ErrorValue{ErrorType::Value};
        int y1, m1, d1, y2, m2, d2;
        serialToCivil(*s1, y1, m1, d1);
        serialToCivil(*s2, y2, m2, d2);
        d1 = std::min(d1, 30);
        d2 = std::min(d2, 30);
        return static_cast<double>((y2 - y1) * 360 + (m2 - m1) * 30 + (d2 - d1));
    }});
    registry.registerFunction({"YEARFRAC", 2, 3, [](const auto& a, const auto&) -> CellValue {
        auto s1 = toNumber(a[0].scalar), s2 = toNumber(a[1].scalar);
        if (!s1 || !s2) return ErrorValue{ErrorType::Value};
        return std::fabs(std::floor(*s2) - std::floor(*s1)) / 365.0;
    }});
    registry.registerFunction({"WEEKNUM", 1, 2, [](const auto& a, const auto&) -> CellValue {
        auto s = toNumber(a[0].scalar);
        if (!s) return ErrorValue{ErrorType::Value};
        int y, m, d;
        serialToCivil(*s, y, m, d);
        double jan1 = civilToSerial(y, 1, 1);
        return std::floor((std::floor(*s) - jan1) / 7.0) + 1.0;
    }});
    registry.registerFunction({"ISOWEEKNUM", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto s = toNumber(a[0].scalar);
        if (!s) return ErrorValue{ErrorType::Value};
        int y, m, d;
        serialToCivil(*s, y, m, d);
        double jan1 = civilToSerial(y, 1, 1);
        return std::floor((std::floor(*s) - jan1) / 7.0) + 1.0;
    }});

    // ---- Lookup, Logical & Info (20) ----
    registry.registerFunction({"CHOOSE", 2, 255, [](const auto& a, const auto&) -> CellValue {
        auto idx = toNumber(a[0].scalar);
        if (!idx || *idx < 1.0 || static_cast<std::size_t>(*idx) >= a.size()) return ErrorValue{ErrorType::Value};
        return a[static_cast<std::size_t>(*idx)].scalar;
    }});
    registry.registerFunction({"XMATCH", 2, 4, [](const auto& a, const auto& ctx) -> CellValue {
        if (!a[1].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
        auto vals = ctx.values->getRangeValues(a[1].rangeRef->sheetId, a[1].rangeRef->range);
        for (std::size_t i = 0; i < vals.size(); ++i) {
            if (vals[i] == a[0].scalar) return static_cast<double>(i + 1);
        }
        return ErrorValue{ErrorType::NA};
    }});
    registry.registerFunction({"ADDRESS", 2, 5, [](const auto& a, const auto&) -> CellValue {
        auto r = toNumber(a[0].scalar), c = toNumber(a[1].scalar);
        if (!r || !c || *r < 1.0 || *c < 1.0) return ErrorValue{ErrorType::Value};
        int absNum = a.size() >= 3 ? static_cast<int>(toNumber(a[2].scalar).value_or(1.0)) : 1;
        bool absRow = (absNum == 1 || absNum == 2);
        bool absCol = (absNum == 1 || absNum == 3);
        CellCoord coord{static_cast<RowIndex>(*r) - 1, static_cast<ColIndex>(*c) - 1};
        std::string ref = coord.toA1(absCol, absRow);
        if (a.size() >= 5 && !isEmpty(a[4].scalar)) {
            ref = toDisplayString(a[4].scalar) + "!" + ref;
        }
        return ref;
    }});
    registry.registerFunction({"XOR", 1, 255, [](const auto& args, const auto&) -> CellValue {
        bool acc = false;
        for (const auto& a : args) {
            auto b = toBool(a.scalar);
            if (!b) return ErrorValue{ErrorType::Value};
            if (*b) acc = !acc;
        }
        return acc;
    }});
    registry.registerFunction({"TRUE", 0, 0, [](const auto&, const auto&) -> CellValue { return true; }});
    registry.registerFunction({"FALSE", 0, 0, [](const auto&, const auto&) -> CellValue { return false; }});
    registry.registerFunction({"NA", 0, 0, [](const auto&, const auto&) -> CellValue { return ErrorValue{ErrorType::NA}; }});
    registry.registerFunction({"ISLOGICAL", 1, 1, [](const auto& a, const auto&) -> CellValue { return isBool(a[0].scalar); }});
    registry.registerFunction({"ISNONTEXT", 1, 1, [](const auto& a, const auto&) -> CellValue { return !isText(a[0].scalar); }});
    registry.registerFunction({"ISEVEN", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto n = toNumber(a[0].scalar);
        if (!n) return ErrorValue{ErrorType::Value};
        return (static_cast<long long>(std::trunc(*n)) % 2) == 0;
    }});
    registry.registerFunction({"ISODD", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto n = toNumber(a[0].scalar);
        if (!n) return ErrorValue{ErrorType::Value};
        return (static_cast<long long>(std::trunc(*n)) % 2) != 0;
    }});
    registry.registerFunction({"ISNA", 1, 1, [](const auto& a, const auto&) -> CellValue {
        return isError(a[0].scalar) && std::get<ErrorValue>(a[0].scalar).type == ErrorType::NA;
    }});
    registry.registerFunction({"ISERR", 1, 1, [](const auto& a, const auto&) -> CellValue {
        return isError(a[0].scalar) && std::get<ErrorValue>(a[0].scalar).type != ErrorType::NA;
    }});
    registry.registerFunction({"ERROR.TYPE", 1, 1, [](const auto& a, const auto&) -> CellValue {
        if (!isError(a[0].scalar)) return ErrorValue{ErrorType::NA};
        switch (std::get<ErrorValue>(a[0].scalar).type) {
            case ErrorType::Null: return 1.0;
            case ErrorType::DivZero: return 2.0;
            case ErrorType::Value: return 3.0;
            case ErrorType::Ref: return 4.0;
            case ErrorType::Name: return 5.0;
            case ErrorType::Num: return 6.0;
            case ErrorType::NA: return 7.0;
            default: return 8.0;
        }
    }});
    registry.registerFunction({"TYPE", 1, 1, [](const auto& a, const auto&) -> CellValue {
        if (isNumber(a[0].scalar) || isDateTime(a[0].scalar) || isEmpty(a[0].scalar)) return 1.0;
        if (isText(a[0].scalar)) return 2.0;
        if (isBool(a[0].scalar)) return 4.0;
        if (isError(a[0].scalar)) return 16.0;
        return 64.0;
    }});
    registerTextDateInfoExtFunctions(registry);
    registerExternalFunctions(registry);
}

} // namespace sheetcore::func_text_date_info
