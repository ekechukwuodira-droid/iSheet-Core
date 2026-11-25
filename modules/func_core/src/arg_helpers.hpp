#pragma once

#include "sheetcore/core/value.hpp"
#include "sheetcore/formula/context.hpp"

#include <optional>
#include <vector>

namespace sheetcore::func_core::detail {

template <typename Fn>
std::optional<ErrorValue> forEachArgValue(const std::vector<formula::EvalArg>& args,
                                          const formula::EvalContext& ctx,
                                          Fn&& fn) {
    for (const auto& arg : args) {
        if (arg.isRange()) {
            if (!ctx.values) {
                return ErrorValue{ErrorType::Ref};
            }
            auto vals = ctx.values->getRangeValues(arg.rangeRef->sheetId, arg.rangeRef->range);
            for (const auto& v : vals) {
                if (isError(v)) return std::get<ErrorValue>(v);
                fn(v, true);
            }
        } else if (arg.isArray()) {
            for (const auto& v : arg.array->elements) {
                if (isError(v)) return std::get<ErrorValue>(v);
                fn(v, true);
            }
        } else {
            if (isError(arg.scalar)) return std::get<ErrorValue>(arg.scalar);
            fn(arg.scalar, false);
        }
    }
    return std::nullopt;
}

} // namespace sheetcore::func_core::detail
