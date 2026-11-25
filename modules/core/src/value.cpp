#include "sheetcore/core/value.hpp"

#include <charconv>
#include <sstream>

namespace sheetcore {

std::string_view ErrorValue::toString() const noexcept {
    switch (type) {
        case ErrorType::Null: return "#NULL!";
        case ErrorType::DivZero: return "#DIV/0!";
        case ErrorType::Value: return "#VALUE!";
        case ErrorType::Ref: return "#REF!";
        case ErrorType::Name: return "#NAME?";
        case ErrorType::Num: return "#NUM!";
        case ErrorType::NA: return "#N/A";
        case ErrorType::Spill: return "#SPILL!";
        case ErrorType::Circular: return "#CIRCULAR!";
        case ErrorType::Calc: return "#CALC!";
        case ErrorType::Field: return "#FIELD!";
        case ErrorType::Connect: return "#CONNECT!";
        case ErrorType::Blocked: return "#BLOCKED!";
        case ErrorType::Busy: return "#BUSY!";
        case ErrorType::Unknown: return "#UNKNOWN!";
        case ErrorType::GettingData: return "#GETTING_DATA";
    }
    return "#VALUE!";
}

std::optional<ErrorValue> ErrorValue::fromString(std::string_view text) {
    if (text == "#NULL!") return ErrorValue{ErrorType::Null};
    if (text == "#DIV/0!") return ErrorValue{ErrorType::DivZero};
    if (text == "#VALUE!") return ErrorValue{ErrorType::Value};
    if (text == "#REF!") return ErrorValue{ErrorType::Ref};
    if (text == "#NAME?") return ErrorValue{ErrorType::Name};
    if (text == "#NUM!") return ErrorValue{ErrorType::Num};
    if (text == "#N/A") return ErrorValue{ErrorType::NA};
    if (text == "#SPILL!") return ErrorValue{ErrorType::Spill};
    if (text == "#CIRCULAR!") return ErrorValue{ErrorType::Circular};
    if (text == "#CALC!") return ErrorValue{ErrorType::Calc};
    if (text == "#FIELD!") return ErrorValue{ErrorType::Field};
    if (text == "#CONNECT!") return ErrorValue{ErrorType::Connect};
    if (text == "#BLOCKED!") return ErrorValue{ErrorType::Blocked};
    if (text == "#BUSY!") return ErrorValue{ErrorType::Busy};
    if (text == "#UNKNOWN!") return ErrorValue{ErrorType::Unknown};
    if (text == "#GETTING_DATA") return ErrorValue{ErrorType::GettingData};
    return std::nullopt;
}

std::optional<double> toNumber(const CellValue& v) {
    if (const auto* d = std::get_if<double>(&v)) {
        return *d;
    }
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b ? 1.0 : 0.0;
    }
    if (const auto* dt = std::get_if<DateTimeValue>(&v)) {
        return dt->serial;
    }
    if (std::holds_alternative<EmptyValue>(v)) {
        return 0.0;
    }
    if (const auto* s = std::get_if<std::string>(&v)) {
        if (s->empty()) {
            return 0.0;
        }
        double val = 0.0;
        const char* begin = s->data();
        const char* end = s->data() + s->size();
        auto [ptr, ec] = std::from_chars(begin, end, val);
        if (ec == std::errc{} && ptr == end) {
            return val;
        }
    }
    return std::nullopt;
}

std::optional<bool> toBool(const CellValue& v) {
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    if (const auto* d = std::get_if<double>(&v)) {
        return *d != 0.0;
    }
    if (std::holds_alternative<EmptyValue>(v)) {
        return false;
    }
    if (const auto* s = std::get_if<std::string>(&v)) {
        if (*s == "TRUE" || *s == "true") return true;
        if (*s == "FALSE" || *s == "false") return false;
    }
    return std::nullopt;
}

std::string toDisplayString(const CellValue& v) {
    if (std::holds_alternative<EmptyValue>(v)) {
        return {};
    }
    if (const auto* d = std::get_if<double>(&v)) {
        std::ostringstream oss;
        oss << *d;
        return oss.str();
    }
    if (const auto* s = std::get_if<std::string>(&v)) {
        return *s;
    }
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b ? "TRUE" : "FALSE";
    }
    if (const auto* dt = std::get_if<DateTimeValue>(&v)) {
        std::ostringstream oss;
        oss << dt->serial;
        return oss.str();
    }
    if (const auto* err = std::get_if<ErrorValue>(&v)) {
        return std::string(err->toString());
    }
    return {};
}

} // namespace sheetcore
