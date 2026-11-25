#include "sheetcore/format/number_formatter.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace sheetcore::format {

namespace {

std::string formatWithCommas(double val, std::uint8_t decimals, bool useCommas) {
    bool neg = val < 0.0;
    double absVal = std::fabs(val);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << absVal;
    std::string raw = oss.str();

    if (useCommas) {
        auto dotPos = raw.find('.');
        std::string intPart = dotPos == std::string::npos ? raw : raw.substr(0, dotPos);
        std::string fracPart = dotPos == std::string::npos ? "" : raw.substr(dotPos);
        std::string grouped;
        int count = 0;
        for (auto it = intPart.rbegin(); it != intPart.rend(); ++it) {
            if (count > 0 && count % 3 == 0) {
                grouped.push_back(',');
            }
            grouped.push_back(*it);
            ++count;
        }
        std::reverse(grouped.begin(), grouped.end());
        raw = grouped + fracPart;
    }

    return neg ? ("-" + raw) : raw;
}

std::string formatSerialDate(double serial) {
    // Days since 1899-12-30 -> civil calendar (Howard Hinnant's algorithm)
    long long daysSinceEpoch = static_cast<long long>(std::floor(serial)) - 25569LL; // 1970-01-01 is serial 25569
    long long z = daysSinceEpoch + 719468LL;
    long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    unsigned doe = static_cast<unsigned>(z - era * 146097LL);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = static_cast<long long>(yoe) + era * 400LL;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << y << "-"
        << std::setw(2) << m << "-" << std::setw(2) << d;
    return oss.str();
}

std::string formatSerialTime(double serial) {
    double frac = serial - std::floor(serial);
    if (frac < 0.0) frac += 1.0;
    int totalSeconds = static_cast<int>(std::round(frac * 86400.0)) % 86400;
    int h = totalSeconds / 3600;
    int m = (totalSeconds % 3600) / 60;
    int s = totalSeconds % 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << h << ":"
        << std::setw(2) << m << ":" << std::setw(2) << s;
    return oss.str();
}

} // namespace

std::string NumberFormatter::format(const CellValue& value, const NumberFormat& fmt) {
    if (isEmpty(value)) return {};
    if (isError(value) || isBool(value) || isText(value)) {
        return toDisplayString(value);
    }

    double num = isDateTime(value) ? std::get<DateTimeValue>(value).serial : std::get<double>(value);

    switch (fmt.category) {
        case NumberFormatCategory::General:
        case NumberFormatCategory::Text:
            return toDisplayString(value);
        case NumberFormatCategory::Number:
            return formatWithCommas(num, fmt.decimalPlaces, fmt.useThousandsSeparator);
        case NumberFormatCategory::Percentage:
            return formatWithCommas(num * 100.0, fmt.decimalPlaces, fmt.useThousandsSeparator) + "%";
        case NumberFormatCategory::Currency:
        case NumberFormatCategory::Accounting: {
            if (num < 0.0) {
                return "-" + fmt.currencySymbol + formatWithCommas(std::fabs(num), fmt.decimalPlaces, true);
            }
            return fmt.currencySymbol + formatWithCommas(num, fmt.decimalPlaces, true);
        }
        case NumberFormatCategory::Scientific: {
            std::ostringstream oss;
            oss << std::scientific << std::uppercase << std::setprecision(fmt.decimalPlaces) << num;
            return oss.str();
        }
        case NumberFormatCategory::Date:
            return formatSerialDate(num);
        case NumberFormatCategory::Time:
            return formatSerialTime(num);
        case NumberFormatCategory::Fraction: {
            long long whole = static_cast<long long>(std::trunc(num));
            double frac = std::fabs(num - static_cast<double>(whole));
            int denom = 8;
            int numer = static_cast<int>(std::round(frac * denom));
            if (numer == 0) return std::to_string(whole);
            if (numer == denom) return std::to_string(whole + (num >= 0 ? 1 : -1));
            std::ostringstream oss;
            if (whole != 0) oss << whole << " ";
            oss << numer << "/" << denom;
            return oss.str();
        }
        case NumberFormatCategory::Custom:
            if (fmt.formatCode.find('%') != std::string::npos) {
                return formatWithCommas(num * 100.0, fmt.decimalPlaces, fmt.useThousandsSeparator) + "%";
            }
            return formatWithCommas(num, fmt.decimalPlaces, fmt.useThousandsSeparator);
    }
    return toDisplayString(value);
}

} // namespace sheetcore::format
