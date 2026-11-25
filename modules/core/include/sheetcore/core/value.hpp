#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace sheetcore {

struct EmptyValue {
    constexpr bool operator==(const EmptyValue&) const noexcept = default;
};

struct DateTimeValue {
    // Days since 1899-12-30 (Excel serial date convention, fractional part = time of day).
    double serial = 0.0;

    constexpr auto operator<=>(const DateTimeValue&) const = default;
};

enum class ErrorType : std::uint8_t {
    Null,
    DivZero,
    Value,
    Ref,
    Name,
    Num,
    NA,
    Spill,
    Circular,
    // Appended for Excel 365 parity (keep order stable for serialized codecs).
    Calc,       // #CALC!   (e.g. empty FILTER result, un-invoked LAMBDA)
    Field,      // #FIELD!
    Connect,    // #CONNECT! (external data source unavailable)
    Blocked,    // #BLOCKED! (feature disabled / host does not permit)
    Busy,       // #BUSY!
    Unknown,    // #UNKNOWN!
    GettingData // #GETTING_DATA
};

struct ErrorValue {
    ErrorType type = ErrorType::Value;

    constexpr bool operator==(const ErrorValue&) const noexcept = default;
    std::string_view toString() const noexcept;
    static std::optional<ErrorValue> fromString(std::string_view text);
};

using CellValue = std::variant<
    EmptyValue,
    double,
    std::string,
    bool,
    DateTimeValue,
    ErrorValue
>;

inline bool isEmpty(const CellValue& v) noexcept { return std::holds_alternative<EmptyValue>(v); }
inline bool isNumber(const CellValue& v) noexcept { return std::holds_alternative<double>(v); }
inline bool isText(const CellValue& v) noexcept { return std::holds_alternative<std::string>(v); }
inline bool isBool(const CellValue& v) noexcept { return std::holds_alternative<bool>(v); }
inline bool isDateTime(const CellValue& v) noexcept { return std::holds_alternative<DateTimeValue>(v); }
inline bool isError(const CellValue& v) noexcept { return std::holds_alternative<ErrorValue>(v); }

std::optional<double> toNumber(const CellValue& v);
std::optional<bool> toBool(const CellValue& v);
std::string toDisplayString(const CellValue& v);

} // namespace sheetcore
