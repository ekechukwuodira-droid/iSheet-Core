#include "sheetcore/func_math_trig/register.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>

namespace sheetcore::func_math_trig {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<double> collectNumbers(const std::vector<formula::EvalArg>& args,
                                   const formula::EvalContext& ctx,
                                   std::size_t startIdx = 0) {
    std::vector<double> out;
    for (std::size_t i = startIdx; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a.isRange() && ctx.values) {
            for (const auto& v : ctx.values->getRangeValues(a.rangeRef->sheetId, a.rangeRef->range)) {
                if (isNumber(v)) out.push_back(std::get<double>(v));
            }
        } else if (a.isArray()) {
            for (const auto& v : a.array->elements) {
                if (isNumber(v)) out.push_back(std::get<double>(v));
            }
        } else if (auto n = toNumber(a.scalar)) {
            out.push_back(*n);
        }
    }
    return out;
}

template <typename Fn>
formula::FunctionImpl unaryMath(Fn&& fn) {
    return [fn = std::forward<Fn>(fn)](const std::vector<formula::EvalArg>& args, const formula::EvalContext&) -> CellValue {
        if (isError(args[0].scalar)) return args[0].scalar;
        auto n = toNumber(args[0].scalar);
        if (!n) return ErrorValue{ErrorType::Value};
        double r = fn(*n);
        if (std::isnan(r) || std::isinf(r)) return ErrorValue{ErrorType::Num};
        return r;
    };
}

std::uint64_t gcdU64(std::uint64_t a, std::uint64_t b) {
    while (b != 0) {
        std::uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

double factorial(int n) {
    double r = 1.0;
    for (int i = 2; i <= n; ++i) r *= static_cast<double>(i);
    return r;
}

CellValue fnLog(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar);
    double base = args.size() >= 2 ? toNumber(args[1].scalar).value_or(10.0) : 10.0;
    if (!n || *n <= 0.0 || base <= 0.0 || base == 1.0) return ErrorValue{ErrorType::Num};
    return std::log(*n) / std::log(base);
}

CellValue fnAtan2(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    // Excel ATAN2(x_num, y_num) -> atan2(y, x)
    auto x = toNumber(args[0].scalar), y = toNumber(args[1].scalar);
    if (!x || !y) return ErrorValue{ErrorType::Value};
    if (*x == 0.0 && *y == 0.0) return ErrorValue{ErrorType::DivZero};
    return std::atan2(*y, *x);
}

CellValue fnFact(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar);
    if (!n || *n < 0.0 || *n > 170.0) return ErrorValue{ErrorType::Num};
    return factorial(static_cast<int>(std::trunc(*n)));
}

CellValue fnFactDouble(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto nOpt = toNumber(args[0].scalar);
    if (!nOpt || *nOpt < -1.0 || *nOpt > 300.0) return ErrorValue{ErrorType::Num};
    int n = static_cast<int>(std::trunc(*nOpt));
    double r = 1.0;
    for (int i = n; i >= 2; i -= 2) r *= static_cast<double>(i);
    return r;
}

CellValue fnCombin(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto nOpt = toNumber(args[0].scalar), kOpt = toNumber(args[1].scalar);
    if (!nOpt || !kOpt || *nOpt < 0 || *kOpt < 0 || *kOpt > *nOpt) return ErrorValue{ErrorType::Num};
    int n = static_cast<int>(std::trunc(*nOpt));
    int k = static_cast<int>(std::trunc(*kOpt));
    k = std::min(k, n - k);
    double r = 1.0;
    for (int i = 1; i <= k; ++i) r = r * static_cast<double>(n - k + i) / static_cast<double>(i);
    return std::round(r);
}

CellValue fnCombina(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nOpt = toNumber(args[0].scalar), kOpt = toNumber(args[1].scalar);
    if (!nOpt || !kOpt || *nOpt < 0 || *kOpt < 0) return ErrorValue{ErrorType::Num};
    if (*nOpt == 0 && *kOpt == 0) return 1.0;
    std::vector<formula::EvalArg> cArgs{
        formula::EvalArg{std::trunc(*nOpt) + std::trunc(*kOpt) - 1.0, std::nullopt, std::nullopt},
        formula::EvalArg{std::trunc(*kOpt), std::nullopt, std::nullopt}
    };
    return fnCombin(cArgs, ctx);
}

CellValue fnPermut(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto nOpt = toNumber(args[0].scalar), kOpt = toNumber(args[1].scalar);
    if (!nOpt || !kOpt || *nOpt < 0 || *kOpt < 0 || *kOpt > *nOpt) return ErrorValue{ErrorType::Num};
    int n = static_cast<int>(std::trunc(*nOpt)), k = static_cast<int>(std::trunc(*kOpt));
    double r = 1.0;
    for (int i = 0; i < k; ++i) r *= static_cast<double>(n - i);
    return r;
}

CellValue fnPermutationa(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto nOpt = toNumber(args[0].scalar), kOpt = toNumber(args[1].scalar);
    if (!nOpt || !kOpt || *nOpt < 0 || *kOpt < 0) return ErrorValue{ErrorType::Num};
    return std::pow(std::trunc(*nOpt), std::trunc(*kOpt));
}

CellValue fnGcd(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    if (nums.empty()) return ErrorValue{ErrorType::Value};
    std::uint64_t g = 0;
    for (double d : nums) {
        if (d < 0.0) return ErrorValue{ErrorType::Num};
        g = gcdU64(g, static_cast<std::uint64_t>(std::trunc(d)));
    }
    return static_cast<double>(g);
}

CellValue fnLcm(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    if (nums.empty()) return ErrorValue{ErrorType::Value};
    std::uint64_t l = 1;
    for (double d : nums) {
        if (d < 0.0) return ErrorValue{ErrorType::Num};
        std::uint64_t v = static_cast<std::uint64_t>(std::trunc(d));
        if (v == 0) return 0.0;
        l = (l / gcdU64(l, v)) * v;
    }
    return static_cast<double>(l);
}

CellValue fnMultinomial(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    int sum = 0;
    double denom = 1.0;
    for (double d : nums) {
        if (d < 0.0) return ErrorValue{ErrorType::Num};
        int k = static_cast<int>(std::trunc(d));
        sum += k;
        denom *= factorial(k);
    }
    return std::round(factorial(sum) / denom);
}

CellValue fnEven(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar);
    if (!n) return ErrorValue{ErrorType::Value};
    double a = std::ceil(std::fabs(*n) / 2.0) * 2.0;
    return *n < 0.0 ? -a : a;
}

CellValue fnOdd(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar);
    if (!n) return ErrorValue{ErrorType::Value};
    if (*n == 0.0) return 1.0;
    double a = std::ceil(std::fabs(*n));
    if (std::fmod(a, 2.0) == 0.0) a += 1.0;
    return *n < 0.0 ? -a : a;
}

CellValue fnTrunc(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar);
    double digits = args.size() >= 2 ? toNumber(args[1].scalar).value_or(0.0) : 0.0;
    if (!n) return ErrorValue{ErrorType::Value};
    double f = std::pow(10.0, std::trunc(digits));
    return std::trunc(*n * f) / f;
}

CellValue fnMRound(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar), m = toNumber(args[1].scalar);
    if (!n || !m) return ErrorValue{ErrorType::Value};
    if (*m == 0.0) return 0.0;
    if ((*n > 0.0 && *m < 0.0) || (*n < 0.0 && *m > 0.0)) return ErrorValue{ErrorType::Num};
    return std::round(*n / *m) * (*m);
}

CellValue fnQuotient(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto a = toNumber(args[0].scalar), b = toNumber(args[1].scalar);
    if (!a || !b) return ErrorValue{ErrorType::Value};
    if (*b == 0.0) return ErrorValue{ErrorType::DivZero};
    return std::trunc(*a / *b);
}

CellValue fnProduct(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    if (nums.empty()) return 0.0;
    double p = 1.0;
    for (double d : nums) p *= d;
    return p;
}

CellValue fnSumSq(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    double s = 0.0;
    for (double d : nums) s += d * d;
    return s;
}

CellValue fnSumXY(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx, int mode) {
    auto x = collectNumbers({args[0]}, ctx);
    auto y = collectNumbers({args[1]}, ctx);
    if (x.size() != y.size()) return ErrorValue{ErrorType::NA};
    double s = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (mode == 0) s += (x[i] * x[i] - y[i] * y[i]);
        else if (mode == 1) s += (x[i] * x[i] + y[i] * y[i]);
        else { double d = x[i] - y[i]; s += d * d; }
    }
    return s;
}

CellValue fnRoman(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto nOpt = toNumber(args[0].scalar);
    if (!nOpt || *nOpt < 0.0 || *nOpt >= 4000.0) return ErrorValue{ErrorType::Value};
    int n = static_cast<int>(std::trunc(*nOpt));
    static const std::pair<int, const char*> kMap[] = {
        {1000,"M"},{900,"CM"},{500,"D"},{400,"CD"},{100,"C"},{90,"XC"},
        {50,"L"},{40,"XL"},{10,"X"},{9,"IX"},{5,"V"},{4,"IV"},{1,"I"}
    };
    std::string out;
    for (const auto& [val, sym] : kMap) {
        while (n >= val) { out += sym; n -= val; }
    }
    return out;
}

CellValue fnArabic(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    std::string s = toDisplayString(args[0].scalar);
    auto rval = [](char c) -> int {
        switch (std::toupper(static_cast<unsigned char>(c))) {
            case 'I': return 1; case 'V': return 5; case 'X': return 10;
            case 'L': return 50; case 'C': return 100; case 'D': return 500; case 'M': return 1000;
            default: return 0;
        }
    };
    int total = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        int cur = rval(s[i]);
        if (cur == 0) return ErrorValue{ErrorType::Value};
        int nxt = (i + 1 < s.size()) ? rval(s[i + 1]) : 0;
        total += (cur < nxt) ? -cur : cur;
    }
    return static_cast<double>(total);
}

CellValue fnBase(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto nOpt = toNumber(args[0].scalar), rOpt = toNumber(args[1].scalar);
    if (!nOpt || !rOpt || *nOpt < 0.0 || *rOpt < 2.0 || *rOpt > 36.0) return ErrorValue{ErrorType::Num};
    std::uint64_t n = static_cast<std::uint64_t>(std::trunc(*nOpt));
    unsigned radix = static_cast<unsigned>(std::trunc(*rOpt));
    std::size_t minLen = args.size() >= 3 ? static_cast<std::size_t>(std::max(0.0, toNumber(args[2].scalar).value_or(0.0))) : 0;
    static const char* kDigits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string out;
    if (n == 0) out = "0";
    else {
        while (n > 0) { out.push_back(kDigits[n % radix]); n /= radix; }
        std::reverse(out.begin(), out.end());
    }
    if (out.size() < minLen) out.insert(out.begin(), minLen - out.size(), '0');
    return out;
}

CellValue fnDecimal(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    std::string s = toDisplayString(args[0].scalar);
    auto rOpt = toNumber(args[1].scalar);
    if (!rOpt || *rOpt < 2.0 || *rOpt > 36.0) return ErrorValue{ErrorType::Num};
    unsigned radix = static_cast<unsigned>(std::trunc(*rOpt));
    double acc = 0.0;
    for (char ch : s) {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (std::isspace(uc)) continue;
        int d = -1;
        if (std::isdigit(uc)) d = ch - '0';
        else if (std::isalpha(uc)) d = std::toupper(uc) - 'A' + 10;
        if (d < 0 || static_cast<unsigned>(d) >= radix) return ErrorValue{ErrorType::Num};
        acc = acc * static_cast<double>(radix) + static_cast<double>(d);
    }
    return acc;
}

CellValue fnSubtotal(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto codeOpt = toNumber(args[0].scalar);
    if (!codeOpt) return ErrorValue{ErrorType::Value};
    int code = static_cast<int>(std::trunc(*codeOpt));
    if (code > 100) code -= 100;
    auto nums = collectNumbers(args, ctx, 1);
    switch (code) {
        case 1: return nums.empty() ? CellValue{ErrorValue{ErrorType::DivZero}}
                                    : CellValue{std::accumulate(nums.begin(), nums.end(), 0.0) / static_cast<double>(nums.size())};
        case 2: case 3: return static_cast<double>(nums.size());
        case 4: return nums.empty() ? 0.0 : *std::max_element(nums.begin(), nums.end());
        case 5: return nums.empty() ? 0.0 : *std::min_element(nums.begin(), nums.end());
        case 6: { double p = 1.0; for (double d : nums) p *= d; return nums.empty() ? 0.0 : p; }
        case 9: return std::accumulate(nums.begin(), nums.end(), 0.0);
        default: return ErrorValue{ErrorType::Value};
    }
}

} // namespace

void registerMathTrigExtFunctions(formula::FunctionRegistry& registry);

void registerMathTrigFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"PI", 0, 0, [](const auto&, const auto&) -> CellValue { return kPi; }});
    registry.registerFunction({"SIN", 1, 1, unaryMath([](double x) { return std::sin(x); })});
    registry.registerFunction({"COS", 1, 1, unaryMath([](double x) { return std::cos(x); })});
    registry.registerFunction({"TAN", 1, 1, unaryMath([](double x) { return std::tan(x); })});
    registry.registerFunction({"ASIN", 1, 1, unaryMath([](double x) { return std::asin(x); })});
    registry.registerFunction({"ACOS", 1, 1, unaryMath([](double x) { return std::acos(x); })});
    registry.registerFunction({"ATAN", 1, 1, unaryMath([](double x) { return std::atan(x); })});
    registry.registerFunction({"ATAN2", 2, 2, fnAtan2});
    registry.registerFunction({"SINH", 1, 1, unaryMath([](double x) { return std::sinh(x); })});
    registry.registerFunction({"COSH", 1, 1, unaryMath([](double x) { return std::cosh(x); })});
    registry.registerFunction({"TANH", 1, 1, unaryMath([](double x) { return std::tanh(x); })});
    registry.registerFunction({"ASINH", 1, 1, unaryMath([](double x) { return std::asinh(x); })});
    registry.registerFunction({"ACOSH", 1, 1, unaryMath([](double x) { return std::acosh(x); })});
    registry.registerFunction({"ATANH", 1, 1, unaryMath([](double x) { return std::atanh(x); })});
    registry.registerFunction({"SEC", 1, 1, unaryMath([](double x) { return 1.0 / std::cos(x); })});
    registry.registerFunction({"CSC", 1, 1, unaryMath([](double x) { return 1.0 / std::sin(x); })});
    registry.registerFunction({"COT", 1, 1, unaryMath([](double x) { return 1.0 / std::tan(x); })});
    registry.registerFunction({"SECH", 1, 1, unaryMath([](double x) { return 1.0 / std::cosh(x); })});
    registry.registerFunction({"CSCH", 1, 1, unaryMath([](double x) { return 1.0 / std::sinh(x); })});
    registry.registerFunction({"COTH", 1, 1, unaryMath([](double x) { return 1.0 / std::tanh(x); })});
    registry.registerFunction({"DEGREES", 1, 1, unaryMath([](double x) { return x * 180.0 / kPi; })});
    registry.registerFunction({"RADIANS", 1, 1, unaryMath([](double x) { return x * kPi / 180.0; })});

    registry.registerFunction({"EXP", 1, 1, unaryMath([](double x) { return std::exp(x); })});
    registry.registerFunction({"LN", 1, 1, unaryMath([](double x) { return std::log(x); })});
    registry.registerFunction({"LOG", 1, 2, fnLog});
    registry.registerFunction({"LOG10", 1, 1, unaryMath([](double x) { return std::log10(x); })});
    registry.registerFunction({"SQRTPI", 1, 1, unaryMath([](double x) { return std::sqrt(x * kPi); })});

    registry.registerFunction({"FACT", 1, 1, fnFact});
    registry.registerFunction({"FACTDOUBLE", 1, 1, fnFactDouble});
    registry.registerFunction({"COMBIN", 2, 2, fnCombin});
    registry.registerFunction({"COMBINA", 2, 2, fnCombina});
    registry.registerFunction({"PERMUT", 2, 2, fnPermut});
    registry.registerFunction({"PERMUTATIONA", 2, 2, fnPermutationa});
    registry.registerFunction({"GCD", 1, 255, fnGcd});
    registry.registerFunction({"LCM", 1, 255, fnLcm});
    registry.registerFunction({"MULTINOMIAL", 1, 255, fnMultinomial});

    registry.registerFunction({"EVEN", 1, 1, fnEven});
    registry.registerFunction({"ODD", 1, 1, fnOdd});
    registry.registerFunction({"TRUNC", 1, 2, fnTrunc});
    registry.registerFunction({"MROUND", 2, 2, fnMRound});
    registry.registerFunction({"QUOTIENT", 2, 2, fnQuotient});
    registry.registerFunction({"PRODUCT", 1, 255, fnProduct});
    registry.registerFunction({"SUMSQ", 1, 255, fnSumSq});
    registry.registerFunction({"SUMX2MY2", 2, 2, [](const auto& a, const auto& c) { return fnSumXY(a, c, 0); }});
    registry.registerFunction({"SUMX2PY2", 2, 2, [](const auto& a, const auto& c) { return fnSumXY(a, c, 1); }});
    registry.registerFunction({"SUMXMY2", 2, 2, [](const auto& a, const auto& c) { return fnSumXY(a, c, 2); }});
    registry.registerFunction({"ROMAN", 1, 2, fnRoman});
    registry.registerFunction({"ARABIC", 1, 1, fnArabic});
    registry.registerFunction({"BASE", 2, 3, fnBase});
    registry.registerFunction({"DECIMAL", 2, 2, fnDecimal});
    registry.registerFunction({"SUBTOTAL", 2, 255, fnSubtotal});
    registerMathTrigExtFunctions(registry);
}

} // namespace sheetcore::func_math_trig
