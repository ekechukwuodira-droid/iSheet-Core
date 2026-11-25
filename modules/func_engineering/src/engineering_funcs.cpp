#include "sheetcore/func_engineering/register.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <complex>
#include <cstdint>
#include <sstream>
#include <string>

namespace sheetcore::func_engineering {

namespace {

std::optional<std::int64_t> parseRadix(std::string_view s, int base) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    if (s.empty() || s.size() > 10) return std::nullopt;
    std::int64_t val = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val, base);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return val;
}

std::string formatRadix(std::int64_t val, int base, std::size_t places) {
    if (val < 0) return "";
    static const char* kDigits = "0123456789ABCDEF";
    std::string out;
    std::uint64_t u = static_cast<std::uint64_t>(val);
    if (u == 0) out = "0";
    else {
        while (u > 0) { out.push_back(kDigits[u % base]); u /= base; }
        std::reverse(out.begin(), out.end());
    }
    if (places > out.size()) out.insert(out.begin(), places - out.size(), '0');
    return out;
}

formula::FunctionImpl makeRadixToDec(int base) {
    return [base](const std::vector<formula::EvalArg>& args, const formula::EvalContext&) -> CellValue {
        auto v = parseRadix(toDisplayString(args[0].scalar), base);
        return v ? CellValue{static_cast<double>(*v)} : CellValue{ErrorValue{ErrorType::Num}};
    };
}

formula::FunctionImpl makeDecToRadix(int base) {
    return [base](const std::vector<formula::EvalArg>& args, const formula::EvalContext&) -> CellValue {
        auto n = toNumber(args[0].scalar);
        if (!n || *n < 0.0) return ErrorValue{ErrorType::Num};
        std::size_t places = args.size() >= 2 ? static_cast<std::size_t>(std::max(0.0, toNumber(args[1].scalar).value_or(0.0))) : 0;
        return formatRadix(static_cast<std::int64_t>(std::trunc(*n)), base, places);
    };
}

formula::FunctionImpl makeRadixToRadix(int fromBase, int toBase) {
    return [fromBase, toBase](const std::vector<formula::EvalArg>& args, const formula::EvalContext&) -> CellValue {
        auto v = parseRadix(toDisplayString(args[0].scalar), fromBase);
        if (!v || *v < 0) return ErrorValue{ErrorType::Num};
        std::size_t places = args.size() >= 2 ? static_cast<std::size_t>(std::max(0.0, toNumber(args[1].scalar).value_or(0.0))) : 0;
        return formatRadix(*v, toBase, places);
    };
}

// Complex number parsing/formatting ("3+4i", "-2-5j", "i", "5")
struct ParsedComplex {
    std::complex<double> z{0.0, 0.0};
    char suffix = 'i';
};

std::optional<ParsedComplex> parseComplex(const CellValue& v) {
    if (isNumber(v)) return ParsedComplex{std::complex<double>(std::get<double>(v), 0.0), 'i'};
    std::string s = toDisplayString(v);
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
    if (s.empty()) return ParsedComplex{};

    char suffix = 'i';
    bool hasImag = (s.back() == 'i' || s.back() == 'j');
    if (hasImag) {
        suffix = s.back();
        s.pop_back();
        if (s.empty() || s == "+") return ParsedComplex{{0.0, 1.0}, suffix};
        if (s == "-") return ParsedComplex{{0.0, -1.0}, suffix};

        // Find split between real and imaginary part (last +/- not preceded by e/E and not at index 0)
        std::size_t split = std::string::npos;
        for (std::size_t i = s.size(); i-- > 1;) {
            if ((s[i] == '+' || s[i] == '-') && s[i - 1] != 'e' && s[i - 1] != 'E') {
                split = i;
                break;
            }
        }
        if (split == std::string::npos) {
            double im = 0.0;
            auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), im);
            if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
            return ParsedComplex{{0.0, im}, suffix};
        }
        double re = 0.0, im = 0.0;
        auto [p1, ec1] = std::from_chars(s.data(), s.data() + split, re);
        if (ec1 != std::errc{} || p1 != s.data() + split) return std::nullopt;
        std::string_view imStr = std::string_view(s).substr(split);
        if (imStr == "+") im = 1.0;
        else if (imStr == "-") im = -1.0;
        else {
            if (imStr.front() == '+') imStr.remove_prefix(1);
            auto [p2, ec2] = std::from_chars(imStr.data(), imStr.data() + imStr.size(), im);
            if (ec2 != std::errc{} || p2 != imStr.data() + imStr.size()) return std::nullopt;
        }
        return ParsedComplex{{re, im}, suffix};
    }

    double re = 0.0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), re);
    if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
    return ParsedComplex{{re, 0.0}, 'i'};
}

std::string formatComplex(std::complex<double> z, char suffix = 'i') {
    double re = std::fabs(z.real()) < 1e-12 ? 0.0 : z.real();
    double im = std::fabs(z.imag()) < 1e-12 ? 0.0 : z.imag();
    std::ostringstream oss;
    if (im == 0.0) {
        oss << re;
    } else if (re == 0.0) {
        if (im == 1.0) oss << suffix;
        else if (im == -1.0) oss << '-' << suffix;
        else oss << im << suffix;
    } else {
        oss << re << (im > 0.0 ? "+" : "");
        if (im == 1.0) oss << suffix;
        else if (im == -1.0) oss << '-' << suffix;
        else oss << im << suffix;
    }
    return oss.str();
}

template <typename Fn>
formula::FunctionImpl unaryComplex(Fn&& fn) {
    return [fn = std::forward<Fn>(fn)](const std::vector<formula::EvalArg>& args, const formula::EvalContext&) -> CellValue {
        auto c = parseComplex(args[0].scalar);
        if (!c) return ErrorValue{ErrorType::Num};
        return formatComplex(fn(c->z), c->suffix);
    };
}

} // namespace

void registerEngineeringFunctions(formula::FunctionRegistry& registry) {
    // 12 Radix conversions
    registry.registerFunction({"BIN2DEC", 1, 1, makeRadixToDec(2)});
    registry.registerFunction({"BIN2HEX", 1, 2, makeRadixToRadix(2, 16)});
    registry.registerFunction({"BIN2OCT", 1, 2, makeRadixToRadix(2, 8)});
    registry.registerFunction({"DEC2BIN", 1, 2, makeDecToRadix(2)});
    registry.registerFunction({"DEC2HEX", 1, 2, makeDecToRadix(16)});
    registry.registerFunction({"DEC2OCT", 1, 2, makeDecToRadix(8)});
    registry.registerFunction({"HEX2BIN", 1, 2, makeRadixToRadix(16, 2)});
    registry.registerFunction({"HEX2DEC", 1, 1, makeRadixToDec(16)});
    registry.registerFunction({"HEX2OCT", 1, 2, makeRadixToRadix(16, 8)});
    registry.registerFunction({"OCT2BIN", 1, 2, makeRadixToRadix(8, 2)});
    registry.registerFunction({"OCT2DEC", 1, 1, makeRadixToDec(8)});
    registry.registerFunction({"OCT2HEX", 1, 2, makeRadixToRadix(8, 16)});

    // 5 Bitwise functions
    registry.registerFunction({"BITAND", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), y = toNumber(a[1].scalar);
        if (!x || !y || *x < 0 || *y < 0) return ErrorValue{ErrorType::Num};
        return static_cast<double>(static_cast<std::uint64_t>(*x) & static_cast<std::uint64_t>(*y));
    }});
    registry.registerFunction({"BITOR", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), y = toNumber(a[1].scalar);
        if (!x || !y || *x < 0 || *y < 0) return ErrorValue{ErrorType::Num};
        return static_cast<double>(static_cast<std::uint64_t>(*x) | static_cast<std::uint64_t>(*y));
    }});
    registry.registerFunction({"BITXOR", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), y = toNumber(a[1].scalar);
        if (!x || !y || *x < 0 || *y < 0) return ErrorValue{ErrorType::Num};
        return static_cast<double>(static_cast<std::uint64_t>(*x) ^ static_cast<std::uint64_t>(*y));
    }});
    registry.registerFunction({"BITLSHIFT", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), s = toNumber(a[1].scalar);
        if (!x || !s || *x < 0) return ErrorValue{ErrorType::Num};
        int sh = static_cast<int>(std::trunc(*s));
        return sh >= 0 ? static_cast<double>(static_cast<std::uint64_t>(*x) << sh)
                       : static_cast<double>(static_cast<std::uint64_t>(*x) >> (-sh));
    }});
    registry.registerFunction({"BITRSHIFT", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), s = toNumber(a[1].scalar);
        if (!x || !s || *x < 0) return ErrorValue{ErrorType::Num};
        int sh = static_cast<int>(std::trunc(*s));
        return sh >= 0 ? static_cast<double>(static_cast<std::uint64_t>(*x) >> sh)
                       : static_cast<double>(static_cast<std::uint64_t>(*x) << (-sh));
    }});

    // 6 Comparison & Error functions
    registry.registerFunction({"DELTA", 1, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar);
        double y = a.size() >= 2 ? toNumber(a[1].scalar).value_or(0.0) : 0.0;
        if (!x) return ErrorValue{ErrorType::Value};
        return (std::fabs(*x - y) <= 1e-12) ? 1.0 : 0.0;
    }});
    registry.registerFunction({"GESTEP", 1, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar);
        double step = a.size() >= 2 ? toNumber(a[1].scalar).value_or(0.0) : 0.0;
        if (!x) return ErrorValue{ErrorType::Value};
        return (*x >= step) ? 1.0 : 0.0;
    }});
    auto erfFn = [](const std::vector<formula::EvalArg>& a, const formula::EvalContext&) -> CellValue {
        auto lo = toNumber(a[0].scalar);
        if (!lo) return ErrorValue{ErrorType::Value};
        if (a.size() >= 2) {
            auto hi = toNumber(a[1].scalar);
            if (!hi) return ErrorValue{ErrorType::Value};
            return std::erf(*hi) - std::erf(*lo);
        }
        return std::erf(*lo);
    };
    registry.registerFunction({"ERF", 1, 2, erfFn});
    registry.registerFunction({"ERF.PRECISE", 1, 1, erfFn});
    auto erfcFn = [](const std::vector<formula::EvalArg>& a, const formula::EvalContext&) -> CellValue {
        auto x = toNumber(a[0].scalar);
        return x ? CellValue{std::erfc(*x)} : CellValue{ErrorValue{ErrorType::Value}};
    };
    registry.registerFunction({"ERFC", 1, 1, erfcFn});
    registry.registerFunction({"ERFC.PRECISE", 1, 1, erfcFn});

    // 23 Complex number functions
    registry.registerFunction({"IMREAL", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto c = parseComplex(a[0].scalar);
        return c ? CellValue{c->z.real()} : CellValue{ErrorValue{ErrorType::Num}};
    }});
    registry.registerFunction({"IMAGINARY", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto c = parseComplex(a[0].scalar);
        return c ? CellValue{c->z.imag()} : CellValue{ErrorValue{ErrorType::Num}};
    }});
    registry.registerFunction({"IMABS", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto c = parseComplex(a[0].scalar);
        return c ? CellValue{std::abs(c->z)} : CellValue{ErrorValue{ErrorType::Num}};
    }});
    registry.registerFunction({"IMARGUMENT", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto c = parseComplex(a[0].scalar);
        if (!c || (c->z.real() == 0.0 && c->z.imag() == 0.0)) return ErrorValue{ErrorType::DivZero};
        return std::arg(c->z);
    }});
    registry.registerFunction({"IMCONJUGATE", 1, 1, unaryComplex([](auto z) { return std::conj(z); })});
    registry.registerFunction({"IMSQRT", 1, 1, unaryComplex([](auto z) { return std::sqrt(z); })});
    registry.registerFunction({"IMSIN", 1, 1, unaryComplex([](auto z) { return std::sin(z); })});
    registry.registerFunction({"IMCOS", 1, 1, unaryComplex([](auto z) { return std::cos(z); })});
    registry.registerFunction({"IMTAN", 1, 1, unaryComplex([](auto z) { return std::tan(z); })});
    registry.registerFunction({"IMSINH", 1, 1, unaryComplex([](auto z) { return std::sinh(z); })});
    registry.registerFunction({"IMCOSH", 1, 1, unaryComplex([](auto z) { return std::cosh(z); })});
    registry.registerFunction({"IMSEC", 1, 1, unaryComplex([](auto z) { return 1.0 / std::cos(z); })});
    registry.registerFunction({"IMSECH", 1, 1, unaryComplex([](auto z) { return 1.0 / std::cosh(z); })});
    registry.registerFunction({"IMCSC", 1, 1, unaryComplex([](auto z) { return 1.0 / std::sin(z); })});
    registry.registerFunction({"IMCSCH", 1, 1, unaryComplex([](auto z) { return 1.0 / std::sinh(z); })});
    registry.registerFunction({"IMCOT", 1, 1, unaryComplex([](auto z) { return 1.0 / std::tan(z); })});
    registry.registerFunction({"IMEXP", 1, 1, unaryComplex([](auto z) { return std::exp(z); })});
    registry.registerFunction({"IMLN", 1, 1, unaryComplex([](auto z) { return std::log(z); })});
    registry.registerFunction({"IMLOG10", 1, 1, unaryComplex([](auto z) { return std::log10(z); })});
    registry.registerFunction({"IMLOG2", 1, 1, unaryComplex([](auto z) { return std::log(z) / std::log(2.0); })});

    registry.registerFunction({"IMSUM", 1, 255, [](const auto& args, const auto&) -> CellValue {
        std::complex<double> acc{0.0, 0.0};
        char suf = 'i';
        for (const auto& a : args) {
            auto c = parseComplex(a.scalar);
            if (!c) return ErrorValue{ErrorType::Num};
            acc += c->z;
            suf = c->suffix;
        }
        return formatComplex(acc, suf);
    }});
    registry.registerFunction({"IMSUB", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto c1 = parseComplex(a[0].scalar), c2 = parseComplex(a[1].scalar);
        if (!c1 || !c2) return ErrorValue{ErrorType::Num};
        return formatComplex(c1->z - c2->z, c1->suffix);
    }});
    registry.registerFunction({"IMPRODUCT", 1, 255, [](const auto& args, const auto&) -> CellValue {
        std::complex<double> acc{1.0, 0.0};
        char suf = 'i';
        for (const auto& a : args) {
            auto c = parseComplex(a.scalar);
            if (!c) return ErrorValue{ErrorType::Num};
            acc *= c->z;
            suf = c->suffix;
        }
        return formatComplex(acc, suf);
    }});
    registry.registerFunction({"IMDIV", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto c1 = parseComplex(a[0].scalar), c2 = parseComplex(a[1].scalar);
        if (!c1 || !c2 || std::abs(c2->z) == 0.0) return ErrorValue{ErrorType::Num};
        return formatComplex(c1->z / c2->z, c1->suffix);
    }});
    registry.registerFunction({"IMPOWER", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto c = parseComplex(a[0].scalar);
        auto p = toNumber(a[1].scalar);
        if (!c || !p) return ErrorValue{ErrorType::Num};
        return formatComplex(std::pow(c->z, *p), c->suffix);
    }});

    // Bessel functions (Excel truncates order n to integer >= 0)
    registry.registerFunction({"BESSELI", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), n = toNumber(a[1].scalar);
        if (!x || !n) return ErrorValue{ErrorType::Value};
        double nu = std::trunc(*n);
        if (nu < 0.0) return ErrorValue{ErrorType::Num};
        // std::cyl_bessel_i requires x >= 0; for x < 0, I_n(-x) = (-1)^n I_n(x)
        double sign = (*x < 0.0 && (static_cast<long long>(nu) & 1LL) != 0) ? -1.0 : 1.0;
        double res = sign * std::cyl_bessel_i(nu, std::fabs(*x));
        return (std::isnan(res) || std::isinf(res)) ? CellValue{ErrorValue{ErrorType::Num}} : CellValue{res};
    }});
    registry.registerFunction({"BESSELJ", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), n = toNumber(a[1].scalar);
        if (!x || !n) return ErrorValue{ErrorType::Value};
        double nu = std::trunc(*n);
        if (nu < 0.0) return ErrorValue{ErrorType::Num};
        double sign = (*x < 0.0 && (static_cast<long long>(nu) & 1LL) != 0) ? -1.0 : 1.0;
        double res = sign * std::cyl_bessel_j(nu, std::fabs(*x));
        return (std::isnan(res) || std::isinf(res)) ? CellValue{ErrorValue{ErrorType::Num}} : CellValue{res};
    }});
    registry.registerFunction({"BESSELK", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), n = toNumber(a[1].scalar);
        if (!x || !n) return ErrorValue{ErrorType::Value};
        double nu = std::trunc(*n);
        if (nu < 0.0 || *x <= 0.0) return ErrorValue{ErrorType::Num};
        double res = std::cyl_bessel_k(nu, *x);
        return (std::isnan(res) || std::isinf(res)) ? CellValue{ErrorValue{ErrorType::Num}} : CellValue{res};
    }});
    registry.registerFunction({"BESSELY", 2, 2, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), n = toNumber(a[1].scalar);
        if (!x || !n) return ErrorValue{ErrorType::Value};
        double nu = std::trunc(*n);
        if (nu < 0.0 || *x <= 0.0) return ErrorValue{ErrorType::Num};
        double res = std::cyl_neumann(nu, *x);
        return (std::isnan(res) || std::isinf(res)) ? CellValue{ErrorValue{ErrorType::Num}} : CellValue{res};
    }});
}

} // namespace sheetcore::func_engineering
