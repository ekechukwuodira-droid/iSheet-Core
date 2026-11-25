#include "sheetcore/formula/functions.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sheetcore::func_financial {

namespace {

// ---- Civil <-> Excel serial date helpers ----
bool isLeapYear(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int daysInMonth(int y, int m) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && isLeapYear(y)) return 29;
    return kDays[std::clamp(m - 1, 0, 11)];
}

double civilToSerial(int y, int m, int d) {
    while (m > 12) { m -= 12; ++y; }
    while (m < 1) { m += 12; --y; }
    long long yy = y - (m <= 2 ? 1 : 0);
    long long era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = static_cast<unsigned>(yy - era * 400);
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

double addMonths(double serial, int months, bool eomRule) {
    int y = 0, m = 0, d = 0;
    serialToCivil(serial, y, m, d);
    bool wasEom = (d == daysInMonth(y, m));
    int total = (y * 12 + (m - 1)) + months;
    int ny = total >= 0 ? total / 12 : (total - 11) / 12;
    int nm = (total - ny * 12) + 1;
    int dim = daysInMonth(ny, nm);
    int nd = (eomRule && wasEom) ? dim : std::min(d, dim);
    return civilToSerial(ny, nm, nd);
}

double days360(double s1, double s2, bool european) {
    int y1 = 0, m1 = 0, d1 = 0, y2 = 0, m2 = 0, d2 = 0;
    serialToCivil(s1, y1, m1, d1);
    serialToCivil(s2, y2, m2, d2);
    if (european) {
        if (d1 == 31) d1 = 30;
        if (d2 == 31) d2 = 30;
    } else {
        if (d1 == 31) d1 = 30;
        if (d2 == 31 && d1 >= 30) d2 = 30;
    }
    return static_cast<double>((y2 - y1) * 360 + (m2 - m1) * 30 + (d2 - d1));
}

// Day count between s1 and s2 under Excel basis 0..4
double dayCount(double s1, double s2, int basis) {
    s1 = std::floor(s1);
    s2 = std::floor(s2);
    if (basis == 0) return days360(s1, s2, false);
    if (basis == 4) return days360(s1, s2, true);
    return s2 - s1;
}

double daysInBasisYear(double s1, double s2, int basis) {
    if (basis == 0 || basis == 2 || basis == 4) return 360.0;
    if (basis == 3) return 365.0;
    // Basis 1: Actual/Actual
    int y1 = 0, m1 = 0, d1 = 0, y2 = 0, m2 = 0, d2 = 0;
    serialToCivil(s1, y1, m1, d1);
    serialToCivil(s2, y2, m2, d2);
    if (y1 == y2) return isLeapYear(y1) ? 366.0 : 365.0;
    double totalDays = 0.0;
    for (int y = y1; y <= y2; ++y) totalDays += isLeapYear(y) ? 366.0 : 365.0;
    return totalDays / static_cast<double>(y2 - y1 + 1);
}

double yearFracBasis(double s1, double s2, int basis) {
    return dayCount(s1, s2, basis) / daysInBasisYear(s1, s2, basis);
}

bool validFreq(int f) { return f == 1 || f == 2 || f == 4; }
bool validBasis(int b) { return b >= 0 && b <= 4; }

// ---- Coupon schedule helpers ----
// Finds previous coupon date (<= settlement) and next coupon date (> settlement) anchored at maturity.
void couponDates(double settle, double mat, int freq, double& pcd, double& ncd, int& numCoupons) {
    settle = std::floor(settle);
    mat = std::floor(mat);
    int stepMonths = 12 / freq;
    int y = 0, m = 0, d = 0;
    serialToCivil(mat, y, m, d);
    bool eom = (d == daysInMonth(y, m));

    int k = 0;
    double cur = mat;
    while (true) {
        double prev = addMonths(mat, -(k + 1) * stepMonths, eom);
        if (prev <= settle) {
            pcd = prev;
            ncd = cur;
            numCoupons = k + 1;
            return;
        }
        cur = prev;
        ++k;
        if (k > 10000) { // safety guard
            pcd = settle;
            ncd = mat;
            numCoupons = 1;
            return;
        }
    }
}

double coupDaysVal(double pcd, double ncd, int freq, int basis) {
    if (basis == 1) return ncd - pcd;
    if (basis == 3) return 365.0 / static_cast<double>(freq);
    return 360.0 / static_cast<double>(freq);
}

double coupDayBsVal(double settle, double pcd, int basis) {
    return dayCount(pcd, settle, basis);
}

double coupDaysNcVal(double settle, double pcd, double ncd, int freq, int basis) {
    if (basis == 1 || basis == 2 || basis == 3) return ncd - std::floor(settle);
    return coupDaysVal(pcd, ncd, freq, basis) - coupDayBsVal(settle, pcd, basis);
}

// ---- Depreciation: DB, VDB, AMORLINC, AMORDEGRC ----
CellValue fnDb(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto cost = toNumber(a[0].scalar), salvage = toNumber(a[1].scalar),
         life = toNumber(a[2].scalar), period = toNumber(a[3].scalar);
    double month = (a.size() >= 5 && !a[4].isOmitted()) ? toNumber(a[4].scalar).value_or(std::nan("")) : 12.0;
    if (!cost || !salvage || !life || !period || std::isnan(month)) return ErrorValue{ErrorType::Value};
    if (*cost < 0 || *salvage < 0 || *life <= 0 || *period <= 0 || month <= 0 || month > 12) return ErrorValue{ErrorType::Num};
    if (*cost == 0.0) return 0.0;
    double rate = std::round((1.0 - std::pow(*salvage / *cost, 1.0 / *life)) * 1000.0) / 1000.0;
    int p = static_cast<int>(std::trunc(*period));
    int lifeInt = static_cast<int>(std::ceil(*life));
    if (p > lifeInt + (month < 12.0 ? 1 : 0)) return ErrorValue{ErrorType::Num};

    double totalDep = 0.0;
    double dep = 0.0;
    for (int i = 1; i <= p; ++i) {
        if (i == 1) {
            dep = (*cost) * rate * month / 12.0;
        } else if (i == lifeInt + 1 && month < 12.0) {
            dep = ((*cost) - totalDep) * rate * (12.0 - month) / 12.0;
        } else {
            dep = ((*cost) - totalDep) * rate;
        }
        totalDep += dep;
    }
    return dep;
}

double vdbCumulative(double cost, double salvage, double life, double endPeriod, double factor, bool noSwitch) {
    if (endPeriod <= 0.0) return 0.0;
    double book = cost;
    double total = 0.0;
    int fullPeriods = static_cast<int>(std::floor(endPeriod));
    double frac = endPeriod - static_cast<double>(fullPeriods);
    for (int p = 0; p < fullPeriods; ++p) {
        if (book <= salvage) break;
        double ddb = book * (factor / life);
        double remLife = life - static_cast<double>(p);
        double sln = (!noSwitch && remLife > 0.0) ? ((book - salvage) / remLife) : 0.0;
        double dep = std::max(ddb, sln);
        if (book - dep < salvage) dep = book - salvage;
        book -= dep;
        total += dep;
    }
    if (frac > 0.0 && book > salvage) {
        double ddb = book * (factor / life);
        double remLife = life - static_cast<double>(fullPeriods);
        double sln = (!noSwitch && remLife > 0.0) ? ((book - salvage) / remLife) : 0.0;
        double dep = std::max(ddb, sln) * frac;
        if (book - dep < salvage) dep = book - salvage;
        total += dep;
    }
    return total;
}

CellValue fnVdb(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto cost = toNumber(a[0].scalar), salvage = toNumber(a[1].scalar),
         life = toNumber(a[2].scalar), startP = toNumber(a[3].scalar), endP = toNumber(a[4].scalar);
    double factor = (a.size() >= 6 && !a[5].isOmitted()) ? toNumber(a[5].scalar).value_or(std::nan("")) : 2.0;
    bool noSwitch = (a.size() >= 7 && !a[6].isOmitted()) ? toBool(a[6].scalar).value_or(false) : false;
    if (!cost || !salvage || !life || !startP || !endP || std::isnan(factor)) return ErrorValue{ErrorType::Value};
    if (*cost < 0 || *salvage < 0 || *life <= 0 || *startP < 0 || *endP < *startP || *endP > *life || factor <= 0) {
        return ErrorValue{ErrorType::Num};
    }
    return vdbCumulative(*cost, *salvage, *life, *endP, factor, noSwitch) -
           vdbCumulative(*cost, *salvage, *life, *startP, factor, noSwitch);
}

CellValue fnAmorLinc(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto cost = toNumber(a[0].scalar), purchased = toNumber(a[1].scalar), firstPeriod = toNumber(a[2].scalar),
         salvage = toNumber(a[3].scalar), period = toNumber(a[4].scalar), rate = toNumber(a[5].scalar);
    int basis = (a.size() >= 7 && !a[6].isOmitted()) ? static_cast<int>(toNumber(a[6].scalar).value_or(-1.0)) : 0;
    if (!cost || !purchased || !firstPeriod || !salvage || !period || !rate) return ErrorValue{ErrorType::Value};
    if (*cost <= 0 || *salvage < 0 || *salvage >= *cost || *rate <= 0 || *period < 0 || !validBasis(basis) || *firstPeriod < *purchased) {
        return ErrorValue{ErrorType::Num};
    }
    double p0 = (*cost) * (*rate) * yearFracBasis(*purchased, *firstPeriod, basis);
    int p = static_cast<int>(std::trunc(*period));
    if (p == 0) return std::min(p0, *cost - *salvage);
    double rem = (*cost - *salvage) - p0;
    double annual = (*cost) * (*rate);
    double before = rem - annual * static_cast<double>(p - 1);
    if (before <= 0.0) return 0.0;
    return std::min(before, annual);
}

CellValue fnAmorDegrc(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto cost = toNumber(a[0].scalar), purchased = toNumber(a[1].scalar), firstPeriod = toNumber(a[2].scalar),
         salvage = toNumber(a[3].scalar), period = toNumber(a[4].scalar), rate = toNumber(a[5].scalar);
    int basis = (a.size() >= 7 && !a[6].isOmitted()) ? static_cast<int>(toNumber(a[6].scalar).value_or(-1.0)) : 0;
    if (!cost || !purchased || !firstPeriod || !salvage || !period || !rate) return ErrorValue{ErrorType::Value};
    if (*cost <= 0 || *salvage < 0 || *salvage >= *cost || *rate <= 0 || *period < 0 || !validBasis(basis) || *firstPeriod < *purchased) {
        return ErrorValue{ErrorType::Num};
    }
    double life = 1.0 / (*rate);
    double coeff = (life < 3.0) ? 1.0 : ((life < 5.0) ? 1.5 : ((life <= 6.0) ? 2.0 : 2.5));
    double depRate = (*rate) * coeff;
    int p = static_cast<int>(std::trunc(*period));
    double rem = *cost;
    double dep0 = std::round(rem * depRate * yearFracBasis(*purchased, *firstPeriod, basis));
    if (p == 0) return std::min(dep0, *cost - *salvage);
    rem -= dep0;
    int totalPeriods = static_cast<int>(std::ceil(life));
    for (int i = 1; i <= p; ++i) {
        int left = totalPeriods - i;
        double d = 0.0;
        if (left == 1 || left == 2) {
            d = std::round((rem - *salvage) * 0.5);
        } else if (left <= 0) {
            d = std::max(0.0, rem - *salvage);
        } else {
            d = std::round(rem * depRate);
        }
        if (rem - d < *salvage) d = std::max(0.0, rem - *salvage);
        rem -= d;
        if (i == p) return d;
    }
    return 0.0;
}

// ---- Coupon functions ----
bool parseCoupArgs(const std::vector<formula::EvalArg>& a, double& settle, double& mat, int& freq, int& basis) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), f = toNumber(a[2].scalar);
    if (!s || !m || !f) return false;
    settle = std::floor(*s);
    mat = std::floor(*m);
    freq = static_cast<int>(std::trunc(*f));
    basis = (a.size() >= 4 && !a[3].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[3].scalar).value_or(-1.0))) : 0;
    return mat > settle && validFreq(freq) && validBasis(basis);
}

CellValue fnCoupDayBs(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    double s, m, pcd, ncd; int f, b, n;
    if (!parseCoupArgs(a, s, m, f, b)) return ErrorValue{ErrorType::Num};
    couponDates(s, m, f, pcd, ncd, n);
    return coupDayBsVal(s, pcd, b);
}

CellValue fnCoupDays(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    double s, m, pcd, ncd; int f, b, n;
    if (!parseCoupArgs(a, s, m, f, b)) return ErrorValue{ErrorType::Num};
    couponDates(s, m, f, pcd, ncd, n);
    return coupDaysVal(pcd, ncd, f, b);
}

CellValue fnCoupDaysNc(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    double s, m, pcd, ncd; int f, b, n;
    if (!parseCoupArgs(a, s, m, f, b)) return ErrorValue{ErrorType::Num};
    couponDates(s, m, f, pcd, ncd, n);
    return coupDaysNcVal(s, pcd, ncd, f, b);
}

CellValue fnCoupNcd(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    double s, m, pcd, ncd; int f, b, n;
    if (!parseCoupArgs(a, s, m, f, b)) return ErrorValue{ErrorType::Num};
    couponDates(s, m, f, pcd, ncd, n);
    return ncd;
}

CellValue fnCoupNum(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    double s, m, pcd, ncd; int f, b, n;
    if (!parseCoupArgs(a, s, m, f, b)) return ErrorValue{ErrorType::Num};
    couponDates(s, m, f, pcd, ncd, n);
    return static_cast<double>(n);
}

CellValue fnCoupPcd(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    double s, m, pcd, ncd; int f, b, n;
    if (!parseCoupArgs(a, s, m, f, b)) return ErrorValue{ErrorType::Num};
    couponDates(s, m, f, pcd, ncd, n);
    return pcd;
}

// ---- Bond Pricing / Yield / Duration / Accrued Interest ----
double bondPrice(double settle, double mat, double rate, double yld, double redemption, int freq, int basis) {
    double pcd = 0.0, ncd = 0.0;
    int n = 0;
    couponDates(settle, mat, freq, pcd, ncd, n);
    double e = coupDaysVal(pcd, ncd, freq, basis);
    double a = coupDayBsVal(settle, pcd, basis);
    double dsc = coupDaysNcVal(settle, pcd, ncd, freq, basis);
    double c = 100.0 * rate / static_cast<double>(freq);

    if (n == 1) {
        double t1 = dsc / e;
        double num = redemption + c;
        double den = 1.0 + (yld / static_cast<double>(freq)) * t1;
        return num / den - c * (a / e);
    }

    double base = 1.0 + yld / static_cast<double>(freq);
    double pv = redemption / std::pow(base, static_cast<double>(n - 1) + dsc / e);
    for (int k = 1; k <= n; ++k) {
        pv += c / std::pow(base, static_cast<double>(k - 1) + dsc / e);
    }
    pv -= c * (a / e);
    return pv;
}

CellValue fnPrice(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), rate = toNumber(a[2].scalar),
         yld = toNumber(a[3].scalar), red = toNumber(a[4].scalar), freq = toNumber(a[5].scalar);
    int basis = (a.size() >= 7 && !a[6].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[6].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !rate || !yld || !red || !freq) return ErrorValue{ErrorType::Value};
    int f = static_cast<int>(std::trunc(*freq));
    if (*m <= *s || *rate < 0 || *yld < 0 || *red <= 0 || !validFreq(f) || !validBasis(basis)) {
        return ErrorValue{ErrorType::Num};
    }
    return bondPrice(*s, *m, *rate, *yld, *red, f, basis);
}

// Generic Newton + bisection yield solver for any monotonic price(y) function
template <typename PriceFn>
std::optional<double> solveYield(PriceFn&& priceOfY, double targetPrice) {
    double lo = -0.99, hi = 10.0;
    double y = 0.05;
    for (int iter = 0; iter < 100; ++iter) {
        double f = priceOfY(y) - targetPrice;
        if (std::fabs(f) < 1e-11) return y;
        double h = 1e-6;
        double df = (priceOfY(y + h) - priceOfY(y - h)) / (2.0 * h);
        if (std::fabs(df) > 1e-14) {
            double ny = y - f / df;
            if (ny > lo && ny < hi) {
                y = ny;
                continue;
            }
        }
        // Fallback bisection step
        double flo = priceOfY(lo) - targetPrice;
        if ((flo > 0.0) == (f > 0.0)) lo = y;
        else hi = y;
        y = 0.5 * (lo + hi);
    }
    return std::fabs(priceOfY(y) - targetPrice) < 1e-5 ? std::optional<double>(y) : std::nullopt;
}

CellValue fnYield(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), rate = toNumber(a[2].scalar),
         pr = toNumber(a[3].scalar), red = toNumber(a[4].scalar), freq = toNumber(a[5].scalar);
    int basis = (a.size() >= 7 && !a[6].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[6].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !rate || !pr || !red || !freq) return ErrorValue{ErrorType::Value};
    int f = static_cast<int>(std::trunc(*freq));
    if (*m <= *s || *rate < 0 || *pr <= 0 || *red <= 0 || !validFreq(f) || !validBasis(basis)) {
        return ErrorValue{ErrorType::Num};
    }
    auto y = solveYield([&](double yv) { return bondPrice(*s, *m, *rate, yv, *red, f, basis); }, *pr);
    return y ? CellValue{*y} : CellValue{ErrorValue{ErrorType::Num}};
}

CellValue fnDuration(const std::vector<formula::EvalArg>& a, const formula::EvalContext&, bool modified) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), coupon = toNumber(a[2].scalar),
         yld = toNumber(a[3].scalar), freq = toNumber(a[4].scalar);
    int basis = (a.size() >= 6 && !a[5].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[5].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !coupon || !yld || !freq) return ErrorValue{ErrorType::Value};
    int f = static_cast<int>(std::trunc(*freq));
    if (*m <= *s || *coupon < 0 || *yld < 0 || !validFreq(f) || !validBasis(basis)) return ErrorValue{ErrorType::Num};

    double pcd = 0.0, ncd = 0.0;
    int n = 0;
    couponDates(*s, *m, f, pcd, ncd, n);
    double e = coupDaysVal(pcd, ncd, f, basis);
    double dsc = coupDaysNcVal(*s, pcd, ncd, f, basis);
    double frac = dsc / e;
    double base = 1.0 + (*yld) / static_cast<double>(f);
    double c = 100.0 * (*coupon) / static_cast<double>(f);

    double pvSum = 0.0;
    double weightedSum = 0.0;
    for (int k = 1; k <= n; ++k) {
        double tPeriods = static_cast<double>(k - 1) + frac;
        double cf = (k == n) ? (c + 100.0) : c;
        double pv = cf / std::pow(base, tPeriods);
        pvSum += pv;
        weightedSum += (tPeriods / static_cast<double>(f)) * pv;
    }
    if (pvSum == 0.0) return ErrorValue{ErrorType::DivZero};
    double mac = weightedSum / pvSum;
    return modified ? (mac / base) : mac;
}

CellValue fnAccrint(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto issue = toNumber(a[0].scalar), firstInt = toNumber(a[1].scalar), settle = toNumber(a[2].scalar),
         rate = toNumber(a[3].scalar), par = toNumber(a[4].scalar), freq = toNumber(a[5].scalar);
    int basis = (a.size() >= 7 && !a[6].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[6].scalar).value_or(-1.0))) : 0;
    if (!issue || !firstInt || !settle || !rate || !par || !freq) return ErrorValue{ErrorType::Value};
    int f = static_cast<int>(std::trunc(*freq));
    if (*rate <= 0 || *par <= 0 || !validFreq(f) || !validBasis(basis) || *issue >= *settle) {
        return ErrorValue{ErrorType::Num};
    }
    return (*par) * (*rate) * yearFracBasis(*issue, *settle, basis);
}

CellValue fnAccrintM(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto issue = toNumber(a[0].scalar), settle = toNumber(a[1].scalar), rate = toNumber(a[2].scalar);
    double par = (a.size() >= 4 && !a[3].isOmitted()) ? toNumber(a[3].scalar).value_or(std::nan("")) : 1000.0;
    int basis = (a.size() >= 5 && !a[4].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[4].scalar).value_or(-1.0))) : 0;
    if (!issue || !settle || !rate || std::isnan(par)) return ErrorValue{ErrorType::Value};
    if (*rate <= 0 || par <= 0 || !validBasis(basis) || *issue >= *settle) return ErrorValue{ErrorType::Num};
    return par * (*rate) * yearFracBasis(*issue, *settle, basis);
}

CellValue fnDisc(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), pr = toNumber(a[2].scalar), red = toNumber(a[3].scalar);
    int basis = (a.size() >= 5 && !a[4].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[4].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !pr || !red) return ErrorValue{ErrorType::Value};
    if (*m <= *s || *pr <= 0 || *red <= 0 || !validBasis(basis)) return ErrorValue{ErrorType::Num};
    return ((*red - *pr) / (*red)) / yearFracBasis(*s, *m, basis);
}

CellValue fnIntRate(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), inv = toNumber(a[2].scalar), red = toNumber(a[3].scalar);
    int basis = (a.size() >= 5 && !a[4].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[4].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !inv || !red) return ErrorValue{ErrorType::Value};
    if (*m <= *s || *inv <= 0 || *red <= 0 || !validBasis(basis)) return ErrorValue{ErrorType::Num};
    return ((*red - *inv) / (*inv)) / yearFracBasis(*s, *m, basis);
}

CellValue fnPriceDisc(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), disc = toNumber(a[2].scalar), red = toNumber(a[3].scalar);
    int basis = (a.size() >= 5 && !a[4].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[4].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !disc || !red) return ErrorValue{ErrorType::Value};
    if (*m <= *s || *disc <= 0 || *red <= 0 || !validBasis(basis)) return ErrorValue{ErrorType::Num};
    return (*red) * (1.0 - (*disc) * yearFracBasis(*s, *m, basis));
}

CellValue fnReceived(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), inv = toNumber(a[2].scalar), disc = toNumber(a[3].scalar);
    int basis = (a.size() >= 5 && !a[4].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[4].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !inv || !disc) return ErrorValue{ErrorType::Value};
    if (*m <= *s || *inv <= 0 || *disc <= 0 || !validBasis(basis)) return ErrorValue{ErrorType::Num};
    double den = 1.0 - (*disc) * yearFracBasis(*s, *m, basis);
    if (den <= 0.0) return ErrorValue{ErrorType::Num};
    return (*inv) / den;
}

CellValue fnYieldDisc(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), pr = toNumber(a[2].scalar), red = toNumber(a[3].scalar);
    int basis = (a.size() >= 5 && !a[4].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[4].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !pr || !red) return ErrorValue{ErrorType::Value};
    if (*m <= *s || *pr <= 0 || *red <= 0 || !validBasis(basis)) return ErrorValue{ErrorType::Num};
    return ((*red - *pr) / (*pr)) / yearFracBasis(*s, *m, basis);
}

CellValue fnPriceMat(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), issue = toNumber(a[2].scalar),
         rate = toNumber(a[3].scalar), yld = toNumber(a[4].scalar);
    int basis = (a.size() >= 6 && !a[5].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[5].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !issue || !rate || !yld) return ErrorValue{ErrorType::Value};
    if (*m <= *s || *issue >= *s || *rate < 0 || *yld < 0 || !validBasis(basis)) return ErrorValue{ErrorType::Num};
    double dim = yearFracBasis(*issue, *m, basis);
    double dsm = yearFracBasis(*s, *m, basis);
    double aFrac = yearFracBasis(*issue, *s, basis);
    return (100.0 + 100.0 * (*rate) * dim) / (1.0 + (*yld) * dsm) - 100.0 * (*rate) * aFrac;
}

CellValue fnYieldMat(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), issue = toNumber(a[2].scalar),
         rate = toNumber(a[3].scalar), pr = toNumber(a[4].scalar);
    int basis = (a.size() >= 6 && !a[5].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[5].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !issue || !rate || !pr) return ErrorValue{ErrorType::Value};
    if (*m <= *s || *issue >= *s || *rate < 0 || *pr <= 0 || !validBasis(basis)) return ErrorValue{ErrorType::Num};
    double dim = yearFracBasis(*issue, *m, basis);
    double dsm = yearFracBasis(*s, *m, basis);
    double aFrac = yearFracBasis(*issue, *s, basis);
    return ((1.0 + (*rate) * dim) / ((*pr) / 100.0 + (*rate) * aFrac) - 1.0) / dsm;
}

// ---- Odd first / odd last period bond functions ----
double oddFPriceCalc(double settle, double mat, double issue, double firstCoup,
                     double rate, double yld, double red, int freq, int basis) {
    double pcd = 0.0, ncd = 0.0;
    int n = 0;
    couponDates(firstCoup, mat, freq, pcd, ncd, n);
    // Number of remaining coupons including firstCoup
    int numCoup = n + 1;
    double e = coupDaysVal(pcd, ncd, freq, basis);
    double dsc = dayCount(settle, firstCoup, basis);
    double dfc = dayCount(issue, firstCoup, basis);
    double a = dayCount(issue, settle, basis);
    double c = 100.0 * rate / static_cast<double>(freq);
    double base = 1.0 + yld / static_cast<double>(freq);

    double pv = red / std::pow(base, static_cast<double>(numCoup - 1) + dsc / e);
    pv += (c * (dfc / e)) / std::pow(base, dsc / e);
    for (int k = 2; k <= numCoup; ++k) {
        pv += c / std::pow(base, static_cast<double>(k - 1) + dsc / e);
    }
    pv -= c * (a / e);
    return pv;
}

CellValue fnOddFPrice(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), issue = toNumber(a[2].scalar),
         fc = toNumber(a[3].scalar), rate = toNumber(a[4].scalar), yld = toNumber(a[5].scalar),
         red = toNumber(a[6].scalar), freq = toNumber(a[7].scalar);
    int basis = (a.size() >= 9 && !a[8].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[8].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !issue || !fc || !rate || !yld || !red || !freq) return ErrorValue{ErrorType::Value};
    int f = static_cast<int>(std::trunc(*freq));
    if (*issue >= *s || *s >= *fc || *fc >= *m || *rate < 0 || *yld < 0 || *red <= 0 || !validFreq(f) || !validBasis(basis)) {
        return ErrorValue{ErrorType::Num};
    }
    return oddFPriceCalc(*s, *m, *issue, *fc, *rate, *yld, *red, f, basis);
}

CellValue fnOddFYield(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), issue = toNumber(a[2].scalar),
         fc = toNumber(a[3].scalar), rate = toNumber(a[4].scalar), pr = toNumber(a[5].scalar),
         red = toNumber(a[6].scalar), freq = toNumber(a[7].scalar);
    int basis = (a.size() >= 9 && !a[8].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[8].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !issue || !fc || !rate || !pr || !red || !freq) return ErrorValue{ErrorType::Value};
    int f = static_cast<int>(std::trunc(*freq));
    if (*issue >= *s || *s >= *fc || *fc >= *m || *rate < 0 || *pr <= 0 || *red <= 0 || !validFreq(f) || !validBasis(basis)) {
        return ErrorValue{ErrorType::Num};
    }
    auto y = solveYield([&](double yv) { return oddFPriceCalc(*s, *m, *issue, *fc, *rate, yv, *red, f, basis); }, *pr);
    return y ? CellValue{*y} : CellValue{ErrorValue{ErrorType::Num}};
}

double oddLPriceCalc(double settle, double mat, double lastInt,
                     double rate, double yld, double red, int freq, int basis) {
    double e = daysInBasisYear(lastInt, mat, basis) / static_cast<double>(freq);
    double dci = dayCount(lastInt, mat, basis);
    double dsm = dayCount(settle, mat, basis);
    double a = dayCount(lastInt, settle, basis);
    double c = 100.0 * rate / static_cast<double>(freq);
    return (red + c * (dci / e)) / (1.0 + (yld / static_cast<double>(freq)) * (dsm / e)) - c * (a / e);
}

CellValue fnOddLPrice(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), li = toNumber(a[2].scalar),
         rate = toNumber(a[3].scalar), yld = toNumber(a[4].scalar), red = toNumber(a[5].scalar),
         freq = toNumber(a[6].scalar);
    int basis = (a.size() >= 8 && !a[7].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[7].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !li || !rate || !yld || !red || !freq) return ErrorValue{ErrorType::Value};
    int f = static_cast<int>(std::trunc(*freq));
    if (*li >= *s || *s >= *m || *rate < 0 || *yld < 0 || *red <= 0 || !validFreq(f) || !validBasis(basis)) {
        return ErrorValue{ErrorType::Num};
    }
    return oddLPriceCalc(*s, *m, *li, *rate, *yld, *red, f, basis);
}

CellValue fnOddLYield(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto s = toNumber(a[0].scalar), m = toNumber(a[1].scalar), li = toNumber(a[2].scalar),
         rate = toNumber(a[3].scalar), pr = toNumber(a[4].scalar), red = toNumber(a[5].scalar),
         freq = toNumber(a[6].scalar);
    int basis = (a.size() >= 8 && !a[7].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[7].scalar).value_or(-1.0))) : 0;
    if (!s || !m || !li || !rate || !pr || !red || !freq) return ErrorValue{ErrorType::Value};
    int f = static_cast<int>(std::trunc(*freq));
    if (*li >= *s || *s >= *m || *rate < 0 || *pr <= 0 || *red <= 0 || !validFreq(f) || !validBasis(basis)) {
        return ErrorValue{ErrorType::Num};
    }
    auto y = solveYield([&](double yv) { return oddLPriceCalc(*s, *m, *li, *rate, yv, *red, f, basis); }, *pr);
    return y ? CellValue{*y} : CellValue{ErrorValue{ErrorType::Num}};
}

} // namespace

void registerFinancialExtFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"DB", 4, 5, fnDb});
    registry.registerFunction({"VDB", 5, 7, fnVdb});
    registry.registerFunction({"AMORLINC", 6, 7, fnAmorLinc});
    registry.registerFunction({"AMORDEGRC", 6, 7, fnAmorDegrc});

    registry.registerFunction({"COUPDAYBS", 3, 4, fnCoupDayBs});
    registry.registerFunction({"COUPDAYS", 3, 4, fnCoupDays});
    registry.registerFunction({"COUPDAYSNC", 3, 4, fnCoupDaysNc});
    registry.registerFunction({"COUPNCD", 3, 4, fnCoupNcd});
    registry.registerFunction({"COUPNUM", 3, 4, fnCoupNum});
    registry.registerFunction({"COUPPCD", 3, 4, fnCoupPcd});

    registry.registerFunction({"ACCRINT", 6, 8, fnAccrint});
    registry.registerFunction({"ACCRINTM", 3, 5, fnAccrintM});
    registry.registerFunction({"DISC", 4, 5, fnDisc});
    registry.registerFunction({"DURATION", 5, 6, [](const auto& a, const auto& c) { return fnDuration(a, c, false); }});
    registry.registerFunction({"MDURATION", 5, 6, [](const auto& a, const auto& c) { return fnDuration(a, c, true); }});
    registry.registerFunction({"INTRATE", 4, 5, fnIntRate});
    registry.registerFunction({"PRICE", 6, 7, fnPrice});
    registry.registerFunction({"PRICEDISC", 4, 5, fnPriceDisc});
    registry.registerFunction({"PRICEMAT", 5, 6, fnPriceMat});
    registry.registerFunction({"RECEIVED", 4, 5, fnReceived});
    registry.registerFunction({"YIELD", 6, 7, fnYield});
    registry.registerFunction({"YIELDDISC", 4, 5, fnYieldDisc});
    registry.registerFunction({"YIELDMAT", 5, 6, fnYieldMat});

    registry.registerFunction({"ODDFPRICE", 8, 9, fnOddFPrice});
    registry.registerFunction({"ODDFYIELD", 8, 9, fnOddFYield});
    registry.registerFunction({"ODDLPRICE", 7, 8, fnOddLPrice});
    registry.registerFunction({"ODDLYIELD", 7, 8, fnOddLYield});
}

} // namespace sheetcore::func_financial
