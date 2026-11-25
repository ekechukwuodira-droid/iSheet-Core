#include "sheetcore/func_financial/register.hpp"

#include <algorithm>
#include <cmath>

namespace sheetcore::func_financial {

namespace {

double calcPmt(double rate, double nper, double pv, double fv, double type) {
    if (std::fabs(rate) < 1e-12) return -(pv + fv) / nper;
    double fac = std::pow(1.0 + rate, nper);
    return -(rate * (pv * fac + fv)) / ((1.0 + rate * type) * (fac - 1.0));
}

double calcFv(double rate, double nper, double pmt, double pv, double type) {
    if (std::fabs(rate) < 1e-12) return -(pv + pmt * nper);
    double fac = std::pow(1.0 + rate, nper);
    return -(pv * fac + pmt * (1.0 + rate * type) * ((fac - 1.0) / rate));
}

std::vector<double> collectNumbers(const formula::EvalArg& arg, const formula::EvalContext& ctx) {
    std::vector<double> out;
    if (arg.isRange() && ctx.values) {
        for (const auto& v : ctx.values->getRangeValues(arg.rangeRef->sheetId, arg.rangeRef->range)) {
            if (isNumber(v) || isDateTime(v)) out.push_back(toNumber(v).value());
        }
    } else if (arg.isArray()) {
        for (const auto& v : arg.array->elements) {
            if (auto n = toNumber(v)) out.push_back(*n);
        }
    } else if (auto n = toNumber(arg.scalar)) {
        out.push_back(*n);
    }
    return out;
}

CellValue fnNper(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto r = toNumber(args[0].scalar), pmt = toNumber(args[1].scalar), pv = toNumber(args[2].scalar);
    double fv = args.size() >= 4 ? toNumber(args[3].scalar).value_or(0.0) : 0.0;
    double type = args.size() >= 5 ? toNumber(args[4].scalar).value_or(0.0) : 0.0;
    if (!r || !pmt || !pv) return ErrorValue{ErrorType::Value};
    if (std::fabs(*r) < 1e-12) {
        if (*pmt == 0.0) return ErrorValue{ErrorType::DivZero};
        return -(*pv + fv) / (*pmt);
    }
    double z = (*pmt) * (1.0 + (*r) * type) / (*r);
    double num = z - fv;
    double den = (*pv) + z;
    if (num / den <= 0.0) return ErrorValue{ErrorType::Num};
    return std::log(num / den) / std::log(1.0 + (*r));
}

CellValue fnIpmt(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto r = toNumber(args[0].scalar), per = toNumber(args[1].scalar), nper = toNumber(args[2].scalar), pv = toNumber(args[3].scalar);
    double fv = args.size() >= 5 ? toNumber(args[4].scalar).value_or(0.0) : 0.0;
    double type = args.size() >= 6 ? toNumber(args[5].scalar).value_or(0.0) : 0.0;
    if (!r || !per || !nper || !pv || *per < 1.0 || *per > *nper) return ErrorValue{ErrorType::Num};
    if (type != 0.0 && *per == 1.0) return 0.0;
    double pmt = calcPmt(*r, *nper, *pv, fv, type);
    double bal = calcFv(*r, *per - 1.0, pmt, *pv, type);
    return type != 0.0 ? (bal - pmt) * (*r) : bal * (*r);
}

CellValue fnPpmt(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    CellValue ipmt = fnIpmt(args, ctx);
    if (isError(ipmt)) return ipmt;
    auto r = toNumber(args[0].scalar), nper = toNumber(args[2].scalar), pv = toNumber(args[3].scalar);
    double fv = args.size() >= 5 ? toNumber(args[4].scalar).value_or(0.0) : 0.0;
    double type = args.size() >= 6 ? toNumber(args[5].scalar).value_or(0.0) : 0.0;
    double pmt = calcPmt(*r, *nper, *pv, fv, type);
    return pmt - std::get<double>(ipmt);
}

CellValue fnIspmt(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto r = toNumber(args[0].scalar), per = toNumber(args[1].scalar), nper = toNumber(args[2].scalar), pv = toNumber(args[3].scalar);
    if (!r || !per || !nper || !pv || *nper == 0.0) return ErrorValue{ErrorType::DivZero};
    return (*pv) * (*r) * ((*per) / (*nper) - 1.0);
}

CellValue fnCumRange(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx, bool isInterest) {
    auto r = toNumber(args[0].scalar), nper = toNumber(args[1].scalar), pv = toNumber(args[2].scalar);
    auto sp = toNumber(args[3].scalar), ep = toNumber(args[4].scalar), type = toNumber(args[5].scalar);
    if (!r || !nper || !pv || !sp || !ep || !type || *r <= 0.0 || *nper <= 0.0 || *pv <= 0.0 || *sp < 1.0 || *ep < *sp || *ep > *nper) {
        return ErrorValue{ErrorType::Num};
    }
    double sum = 0.0;
    for (int p = static_cast<int>(std::ceil(*sp)); p <= static_cast<int>(std::floor(*ep)); ++p) {
        std::vector<formula::EvalArg> sub{
            args[0],
            formula::EvalArg{static_cast<double>(p), std::nullopt, std::nullopt},
            args[1], args[2],
            formula::EvalArg{0.0, std::nullopt, std::nullopt},
            args[5]
        };
        CellValue v = isInterest ? fnIpmt(sub, ctx) : fnPpmt(sub, ctx);
        if (isError(v)) return v;
        sum += std::get<double>(v);
    }
    return sum;
}

CellValue fnFvSchedule(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto principal = toNumber(args[0].scalar);
    if (!principal) return ErrorValue{ErrorType::Value};
    double acc = *principal;
    for (double r : collectNumbers(args[1], ctx)) acc *= (1.0 + r);
    return acc;
}

CellValue fnXnpv(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto rate = toNumber(args[0].scalar);
    if (!rate || *rate <= -1.0) return ErrorValue{ErrorType::Num};
    auto vals = collectNumbers(args[1], ctx);
    auto dates = collectNumbers(args[2], ctx);
    if (vals.size() != dates.size() || vals.empty()) return ErrorValue{ErrorType::Num};
    double d0 = dates[0];
    double npv = 0.0;
    for (std::size_t i = 0; i < vals.size(); ++i) {
        npv += vals[i] / std::pow(1.0 + *rate, (dates[i] - d0) / 365.0);
    }
    return npv;
}

CellValue fnXirr(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto vals = collectNumbers(args[0], ctx);
    auto dates = collectNumbers(args[1], ctx);
    if (vals.size() != dates.size() || vals.size() < 2) return ErrorValue{ErrorType::Num};
    double d0 = dates[0];
    double r = args.size() >= 3 ? toNumber(args[2].scalar).value_or(0.1) : 0.1;
    for (int iter = 0; iter < 100; ++iter) {
        double f = 0.0, df = 0.0;
        for (std::size_t i = 0; i < vals.size(); ++i) {
            double t = (dates[i] - d0) / 365.0;
            double denom = std::pow(1.0 + r, t);
            f += vals[i] / denom;
            if (t != 0.0) df -= t * vals[i] / (denom * (1.0 + r));
        }
        if (std::fabs(f) < 1e-8) return r;
        if (std::fabs(df) < 1e-12) return ErrorValue{ErrorType::Num};
        r -= f / df;
        if (r <= -1.0) return ErrorValue{ErrorType::Num};
    }
    return ErrorValue{ErrorType::Num};
}

CellValue fnMirr(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto vals = collectNumbers(args[0], ctx);
    auto finR = toNumber(args[1].scalar), reinvR = toNumber(args[2].scalar);
    if (!finR || !reinvR || vals.size() < 2) return ErrorValue{ErrorType::DivZero};
    double n = static_cast<double>(vals.size());
    double fvPos = 0.0, pvNeg = 0.0;
    for (std::size_t i = 0; i < vals.size(); ++i) {
        if (vals[i] >= 0.0) fvPos += vals[i] * std::pow(1.0 + *reinvR, n - 1.0 - static_cast<double>(i));
        else pvNeg += vals[i] / std::pow(1.0 + *finR, static_cast<double>(i));
    }
    if (pvNeg == 0.0 || fvPos == 0.0) return ErrorValue{ErrorType::DivZero};
    return std::pow(-fvPos / pvNeg, 1.0 / (n - 1.0)) - 1.0;
}

CellValue fnEffect(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto nom = toNumber(args[0].scalar), npery = toNumber(args[1].scalar);
    if (!nom || !npery || *nom <= 0.0 || *npery < 1.0) return ErrorValue{ErrorType::Num};
    double n = std::trunc(*npery);
    return std::pow(1.0 + (*nom) / n, n) - 1.0;
}

CellValue fnNominal(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto eff = toNumber(args[0].scalar), npery = toNumber(args[1].scalar);
    if (!eff || !npery || *eff <= 0.0 || *npery < 1.0) return ErrorValue{ErrorType::Num};
    double n = std::trunc(*npery);
    return n * (std::pow(1.0 + (*eff), 1.0 / n) - 1.0);
}

CellValue fnPduration(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto r = toNumber(args[0].scalar), pv = toNumber(args[1].scalar), fv = toNumber(args[2].scalar);
    if (!r || !pv || !fv || *r <= 0.0 || *pv <= 0.0 || *fv <= 0.0) return ErrorValue{ErrorType::Num};
    return (std::log(*fv) - std::log(*pv)) / std::log(1.0 + (*r));
}

CellValue fnRri(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto nper = toNumber(args[0].scalar), pv = toNumber(args[1].scalar), fv = toNumber(args[2].scalar);
    if (!nper || !pv || !fv || *nper <= 0.0 || *pv == 0.0) return ErrorValue{ErrorType::Num};
    return std::pow((*fv) / (*pv), 1.0 / (*nper)) - 1.0;
}

// Depreciation: SLN, SYD, DB, DDB
CellValue fnSln(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto cost = toNumber(args[0].scalar), salv = toNumber(args[1].scalar), life = toNumber(args[2].scalar);
    if (!cost || !salv || !life || *life == 0.0) return ErrorValue{ErrorType::DivZero};
    return (*cost - *salv) / (*life);
}

CellValue fnSyd(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto cost = toNumber(args[0].scalar), salv = toNumber(args[1].scalar), life = toNumber(args[2].scalar), per = toNumber(args[3].scalar);
    if (!cost || !salv || !life || !per || *life <= 0.0 || *per < 1.0 || *per > *life) return ErrorValue{ErrorType::Num};
    return ((*cost - *salv) * (*life - *per + 1.0) * 2.0) / ((*life) * (*life + 1.0));
}

CellValue fnDdb(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto cost = toNumber(args[0].scalar), salv = toNumber(args[1].scalar), life = toNumber(args[2].scalar), per = toNumber(args[3].scalar);
    double factor = args.size() >= 5 ? toNumber(args[4].scalar).value_or(2.0) : 2.0;
    if (!cost || !salv || !life || !per || *cost < 0 || *salv < 0 || *life <= 0 || *per < 1 || *per > *life || factor <= 0) {
        return ErrorValue{ErrorType::Num};
    }
    double book = *cost;
    double dep = 0.0;
    for (int p = 1; p <= static_cast<int>(std::trunc(*per)); ++p) {
        dep = std::min(book * (factor / (*life)), std::max(0.0, book - *salv));
        book -= dep;
    }
    return dep;
}

CellValue fnDollarDe(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto fracD = toNumber(args[0].scalar), frac = toNumber(args[1].scalar);
    if (!fracD || !frac || *frac < 1.0) return ErrorValue{ErrorType::Num};
    double f = std::trunc(*frac);
    double whole = std::trunc(*fracD);
    double rem = (*fracD) - whole;
    double scale = std::pow(10.0, std::ceil(std::log10(f)));
    return whole + (rem * scale) / f;
}

CellValue fnDollarFr(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto decD = toNumber(args[0].scalar), frac = toNumber(args[1].scalar);
    if (!decD || !frac || *frac < 1.0) return ErrorValue{ErrorType::Num};
    double f = std::trunc(*frac);
    double whole = std::trunc(*decD);
    double rem = (*decD) - whole;
    double scale = std::pow(10.0, std::ceil(std::log10(f)));
    return whole + (rem * f) / scale;
}

CellValue fnTBillEq(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto settle = toNumber(args[0].scalar), mat = toNumber(args[1].scalar), disc = toNumber(args[2].scalar);
    if (!settle || !mat || !disc || *mat <= *settle || *disc <= 0.0) return ErrorValue{ErrorType::Num};
    double dsm = std::floor(*mat) - std::floor(*settle);
    return (365.0 * (*disc)) / (360.0 - (*disc) * dsm);
}

CellValue fnTBillPrice(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto settle = toNumber(args[0].scalar), mat = toNumber(args[1].scalar), disc = toNumber(args[2].scalar);
    if (!settle || !mat || !disc || *mat <= *settle || *disc <= 0.0) return ErrorValue{ErrorType::Num};
    double dsm = std::floor(*mat) - std::floor(*settle);
    return 100.0 * (1.0 - (*disc) * dsm / 360.0);
}

CellValue fnTBillYield(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto settle = toNumber(args[0].scalar), mat = toNumber(args[1].scalar), pr = toNumber(args[2].scalar);
    if (!settle || !mat || !pr || *mat <= *settle || *pr <= 0.0) return ErrorValue{ErrorType::Num};
    double dsm = std::floor(*mat) - std::floor(*settle);
    return ((100.0 - (*pr)) / (*pr)) * (360.0 / dsm);
}

} // namespace

void registerFinancialExtFunctions(formula::FunctionRegistry& registry);

void registerFinancialFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"NPER", 3, 5, fnNper});
    registry.registerFunction({"IPMT", 4, 6, fnIpmt});
    registry.registerFunction({"PPMT", 4, 6, fnPpmt});
    registry.registerFunction({"ISPMT", 4, 4, fnIspmt});
    registry.registerFunction({"CUMIPMT", 6, 6, [](const auto& a, const auto& c) { return fnCumRange(a, c, true); }});
    registry.registerFunction({"CUMPRINC", 6, 6, [](const auto& a, const auto& c) { return fnCumRange(a, c, false); }});
    registry.registerFunction({"FVSCHEDULE", 2, 2, fnFvSchedule});
    registry.registerFunction({"XNPV", 3, 3, fnXnpv});
    registry.registerFunction({"XIRR", 2, 3, fnXirr});
    registry.registerFunction({"MIRR", 3, 3, fnMirr});
    registry.registerFunction({"EFFECT", 2, 2, fnEffect});
    registry.registerFunction({"NOMINAL", 2, 2, fnNominal});
    registry.registerFunction({"PDURATION", 3, 3, fnPduration});
    registry.registerFunction({"RRI", 3, 3, fnRri});
    registry.registerFunction({"SLN", 3, 3, fnSln});
    registry.registerFunction({"SYD", 4, 4, fnSyd});
    registry.registerFunction({"DDB", 4, 5, fnDdb});
    registry.registerFunction({"DOLLARDE", 2, 2, fnDollarDe});
    registry.registerFunction({"DOLLARFR", 2, 2, fnDollarFr});
    registry.registerFunction({"TBILLEQ", 3, 3, fnTBillEq});
    registry.registerFunction({"TBILLPRICE", 3, 3, fnTBillPrice});
    registry.registerFunction({"TBILLYIELD", 3, 3, fnTBillYield});
    registerFinancialExtFunctions(registry);
}

} // namespace sheetcore::func_financial
