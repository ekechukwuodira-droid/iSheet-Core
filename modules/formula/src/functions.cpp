#include "sheetcore/formula/functions.hpp"

#include <algorithm>
#include <cctype>

namespace sheetcore::formula {

std::string FunctionRegistry::normalizeName(std::string_view name) {
    std::string upper;
    upper.reserve(name.size());
    for (char ch : name) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    for (std::string_view prefix : {std::string_view("_XLFN._XLWS."), std::string_view("_XLFN."),
                                    std::string_view("_XLWS."), std::string_view("_XLUDF.")}) {
        if (upper.size() > prefix.size() && std::string_view(upper).substr(0, prefix.size()) == prefix) {
            upper.erase(0, prefix.size());
            break;
        }
    }
    return upper;
}

void FunctionRegistry::registerFunction(FunctionDefinition def) {
    std::string key = normalizeName(def.name);
    def.name = key;
    functions_[std::move(key)] = std::move(def);
}

void FunctionRegistry::registerArrayFunction(std::string name, std::size_t minArgs, std::size_t maxArgs,
                                             ArrayFunctionImpl impl, bool isVolatile) {
    FunctionDefinition def;
    def.name = std::move(name);
    def.minArgs = minArgs;
    def.maxArgs = maxArgs;
    def.arrayImpl = std::move(impl);
    def.isVolatile = isVolatile;
    registerFunction(std::move(def));
}

void FunctionRegistry::setVolatile(std::string_view name, bool isVolatile) {
    auto it = functions_.find(normalizeName(name));
    if (it != functions_.end()) it->second.isVolatile = isVolatile;
}

const FunctionDefinition* FunctionRegistry::find(std::string_view name) const {
    auto it = functions_.find(normalizeName(name));
    return it == functions_.end() ? nullptr : &it->second;
}

bool FunctionRegistry::contains(std::string_view name) const {
    return functions_.contains(normalizeName(name));
}

std::vector<std::string> FunctionRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(functions_.size());
    for (const auto& [k, _] : functions_) out.push_back(k);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace sheetcore::formula
